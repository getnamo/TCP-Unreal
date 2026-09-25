
#include "TCPServerComponent.h"
#include "Async/Async.h"
#include "TCPWrapperUtility.h"
#include "SocketSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"


UTCPServerComponent::UTCPServerComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoListen = true;
	bReceiveDataOnGameThread = true;
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	ListenPort = 3000;
	ListenSocketName = TEXT("ue4-tcp-server");
	bDisconnectOnFailedEmit = true;
	bShouldPing = false;
	PingInterval = 10.0f;
	PingMessage = TEXT("<Ping>");

	BufferMaxSize = 2 * 1024 * 1024;	//default roughly 2mb
}

void UTCPServerComponent::StartListenServer(const int32 InListenPort)
{
	FIPv4Address Address;
	FIPv4Address::Parse(TEXT("0.0.0.0"), Address);

	//Create Socket
	FIPv4Endpoint Endpoint(Address, InListenPort);

	ListenSocket = FTcpSocketBuilder(*ListenSocketName)
		//.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferMaxSize);

	ListenSocket->SetReceiveBufferSize(BufferMaxSize, BufferMaxSize);
	ListenSocket->SetSendBufferSize(BufferMaxSize, BufferMaxSize);

	ListenSocket->Listen(8);

	OnListenBegin.Broadcast();
	bShouldListen = true;

	//Game thread callbacks may run after this component is destroyed, guard them with a weak ptr
	TWeakObjectPtr<UTCPServerComponent> WeakThis = this;

	//Start a lambda thread to handle data
	ServerFinishedFuture = FTCPWrapperUtility::RunLambdaOnBackGroundThread([&, WeakThis]()
	{
		uint32 BufferSize = 0;
		TArray<uint8> ReceiveBuffer;
		TArray<TSharedPtr<FTCPClient>> ClientsDisconnected;

		FDateTime LastPing = FDateTime::Now();

		while (bShouldListen)
		{
			//Do we have clients trying to connect? connect them
			bool bHasPendingConnection;
			ListenSocket->HasPendingConnection(bHasPendingConnection);
			if (bHasPendingConnection)
			{
				TSharedPtr<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
				FSocket* Client = ListenSocket->Accept(*Addr,TEXT("tcp-client"));

				if (Client != nullptr)
				{
					const FString AddressString = Addr->ToString(true);

					TSharedPtr<FTCPClient> ClientItem = MakeShareable(new FTCPClient());
					ClientItem->Address = AddressString;
					ClientItem->Socket = Client;

					{
						FScopeLock Lock(&ClientsLock);
						Clients.Add(AddressString, ClientItem);
					}

					AsyncTask(ENamedThreads::GameThread, [WeakThis, AddressString]()
					{
						if (WeakThis.IsValid())
						{
							WeakThis->OnClientConnected.Broadcast(AddressString);
						}
					});
				}
			}

			//Iterate a snapshot so game thread emits/disconnects can't modify the map under us
			TArray<TSharedPtr<FTCPClient>> ClientsSnapshot;
			{
				FScopeLock Lock(&ClientsLock);
				Clients.GenerateValueArray(ClientsSnapshot);
			}

			//Check each endpoint for data
			for (TSharedPtr<FTCPClient>& Client : ClientsSnapshot)
			{
				//Did we disconnect? Note that this almost never changed from connected due to engine bug, instead it will be caught when trying to send data
				
				ESocketConnectionState ConnectionState = ESocketConnectionState::SCS_NotConnected;

				if (Client->Socket != nullptr) {
					ConnectionState = Client->Socket->GetConnectionState();
				}

				if (ConnectionState != ESocketConnectionState::SCS_Connected)
				{
					ClientsDisconnected.Add(Client);
					continue;
				}

				if (Client->Socket->HasPendingData(BufferSize))
				{
					ReceiveBuffer.SetNumUninitialized(BufferSize);
					int32 Read = 0;

					Client->Socket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), Read);

					if (bReceiveDataOnGameThread)
					{
						//Copy buffer so it's still valid on game thread
						TArray<uint8> ReceiveBufferGT;
						ReceiveBufferGT.Append(ReceiveBuffer);

						//Pass the reference to be used on gamethread
						AsyncTask(ENamedThreads::GameThread, [WeakThis, ReceiveBufferGT]()
						{
							if (WeakThis.IsValid())
							{
								WeakThis->OnReceivedBytes.Broadcast(ReceiveBufferGT);
							}
						});
					}
					else
					{
						OnReceivedBytes.Broadcast(ReceiveBuffer);
					}
				}

				//ping check

				if (bShouldPing)
				{
					FDateTime Now = FDateTime::Now();
					float TimeSinceLastPing = (Now - LastPing).GetTotalSeconds();

					if (TimeSinceLastPing > PingInterval)
					{
						LastPing = Now;
						int32 BytesSent = 0;
						bool Sent = Client->Socket->Send(PingData.GetData(), PingData.Num(), BytesSent);
						//UE_LOG(LogTemp, Log, TEXT("ping."));
						if (!Sent)
						{
							//UE_LOG(LogTemp, Log, TEXT("did not send."));
							Client->Socket->Close();
						}
					}
				}
			}

			//Handle disconnections
			if (ClientsDisconnected.Num() > 0)
			{
				for (TSharedPtr<FTCPClient> ClientToRemove : ClientsDisconnected)
				{
					const FString Address = ClientToRemove->Address;
					int32 NumRemoved = 0;
					{
						FScopeLock Lock(&ClientsLock);
						NumRemoved = Clients.Remove(Address);
					}

					//Already removed via DisconnectClient, which broadcast for it
					if (NumRemoved == 0)
					{
						continue;
					}

					AsyncTask(ENamedThreads::GameThread, [WeakThis, Address]()
					{
						if (WeakThis.IsValid())
						{
							WeakThis->OnClientDisconnected.Broadcast(Address);
						}
					});
				}
				ClientsDisconnected.Empty();
			}

			//sleep for 100microns
			FPlatformProcess::Sleep(0.0001f);
		}//end while
	});
}

void UTCPServerComponent::StopListenServer()
{
	if (ListenSocket)
	{
		bShouldListen = false;
		ServerFinishedFuture.Get();

		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;

		{
			FScopeLock Lock(&ClientsLock);
			for (auto ClientPair : Clients)
			{
				ClientPair.Value->Socket->Close();
			}
			Clients.Empty();
		}

		OnListenEnd.Broadcast();
	}
}

bool  UTCPServerComponent::Emit(const TArray<uint8>& Bytes, const FString& ToClient)
{
	TArray<TSharedPtr<FTCPClient>> TargetClients;
	{
		FScopeLock Lock(&ClientsLock);

		//simple multi-cast
		if (ToClient == TEXT("All"))
		{
			Clients.GenerateValueArray(TargetClients);
		}
		//match client address and port
		else if (TSharedPtr<FTCPClient>* Client = Clients.Find(ToClient))
		{
			TargetClients.Add(*Client);
		}
	}

	if (TargetClients.Num() == 0)
	{
		return false;
	}

	//Success is all of the messages emitted successfully
	bool Success = true;
	for (TSharedPtr<FTCPClient>& Client : TargetClients)
	{
		if (Client.IsValid() && Client->Socket)
		{
			int32 BytesSent = 0;
			bool Sent = Client->Socket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
			if (!Sent && bDisconnectOnFailedEmit)
			{
				Client->Socket->Close();
			}
			Success = Sent && Success;
		}
	}
	return Success;
}

void UTCPServerComponent::DisconnectClient(FString ClientAddress /*= TEXT("All")*/, bool bDisconnectNextTick/*=false*/)
{
	TWeakObjectPtr<UTCPServerComponent> WeakThis = this;

	TFunction<void()> DisconnectFunction = [WeakThis, ClientAddress]
	{
		if (!WeakThis.IsValid())
		{
			return;
		}

		TArray<TSharedPtr<FTCPClient>> ClientsToRemove;
		{
			FScopeLock Lock(&WeakThis->ClientsLock);

			if (ClientAddress == TEXT("All"))
			{
				WeakThis->Clients.GenerateValueArray(ClientsToRemove);
				WeakThis->Clients.Empty();
			}
			else
			{
				TSharedPtr<FTCPClient> Client;
				if (WeakThis->Clients.RemoveAndCopyValue(ClientAddress, Client))
				{
					ClientsToRemove.Add(Client);
				}
			}
		}

		//Broadcast outside the lock so listeners can safely call back into the component
		for (TSharedPtr<FTCPClient>& Client : ClientsToRemove)
		{
			if (Client->Socket)
			{
				Client->Socket->Close();
			}
			WeakThis->OnClientDisconnected.Broadcast(Client->Address);
		}
	};

	if (bDisconnectNextTick)
	{
		//disconnect on next tick
		AsyncTask(ENamedThreads::GameThread, DisconnectFunction);
	}
	else
	{
		DisconnectFunction();
	}
}

void UTCPServerComponent::InitializeComponent()
{
	Super::InitializeComponent();

	PingData.Append((uint8*)TCHAR_TO_UTF8(*PingMessage), PingMessage.Len());
}

void UTCPServerComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UTCPServerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShouldAutoListen)
	{
		StartListenServer(ListenPort);
	}
}

void UTCPServerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopListenServer();

	Super::EndPlay(EndPlayReason);
}