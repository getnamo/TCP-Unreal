
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

	//Start a lambda thread to handle data
	ServerFinishedFuture = FTCPWrapperUtility::RunLambdaOnBackGroundThread([&]()
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

				const FString AddressString = Addr->ToString(true);

				TSharedPtr<FTCPClient> ClientItem = MakeShareable(new FTCPClient());
				ClientItem->Address = AddressString;
				ClientItem->Socket = Client;

				Clients.Add(AddressString, ClientItem);	//todo: balance this with remove when clients disconnect

				AsyncTask(ENamedThreads::GameThread, [&, AddressString]()
				{
					OnClientConnected.Broadcast(AddressString);
				});
			}

			//Check each endpoint for data
			for (auto ClientPair : Clients)
			{
				TSharedPtr<FTCPClient> Client = ClientPair.Value;

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
						AsyncTask(ENamedThreads::GameThread, [&, ReceiveBufferGT]()
						{
							OnReceivedBytes.Broadcast(ReceiveBufferGT);
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
					Clients.Remove(Address);
					AsyncTask(ENamedThreads::GameThread, [this, Address]()
					{
						OnClientDisconnected.Broadcast(Address);
					});
				}
				ClientsDisconnected.Empty();
			}

			//sleep for 100microns
			FPlatformProcess::Sleep(0.0001);
		}//end while

		for (auto ClientPair : Clients)
		{
			ClientPair.Value->Socket->Close();
		}
		Clients.Empty();

		//Server ended
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			Clients.Empty();
			OnListenEnd.Broadcast();
		});
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

		OnListenEnd.Broadcast();
	}
}

bool  UTCPServerComponent::Emit(const TArray<uint8>& Bytes, const FString& ToClient)
{
	if (Clients.Num()>0)
	{
		int32 BytesSent = 0;
		//simple multi-cast
		if (ToClient == TEXT("All"))
		{
			//Success is all of the messages emitted successfully
			bool Success = true;
			TArray<TSharedPtr<FTCPClient>> AllClients;

			Clients.GenerateValueArray(AllClients);
			for (TSharedPtr<FTCPClient>& Client : AllClients)
			{
				if (Client.IsValid())
				{
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
		//match client address and port
		else
		{
			TSharedPtr<FTCPClient> Client = Clients[ToClient];

			if (Client.IsValid())
			{
				bool Sent = Client->Socket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
				if (!Sent && bDisconnectOnFailedEmit)
				{
					Client->Socket->Close();
				}
				return Sent;
			}
		}
	}
	return false;
}

void UTCPServerComponent::DisconnectClient(FString ClientAddress /*= TEXT("All")*/, bool bDisconnectNextTick/*=false*/)
{
	TFunction<void()> DisconnectFunction = [this, ClientAddress]
	{
		bool bDisconnectAll = ClientAddress == TEXT("All");

		if (!bDisconnectAll)
		{
			TSharedPtr<FTCPClient> Client = Clients[ClientAddress];

			if (Client.IsValid())
			{
				Client->Socket->Close();
				Clients.Remove(Client->Address);
				OnClientDisconnected.Broadcast(ClientAddress);
			}
		}
		else
		{
			for (auto ClientPair : Clients)
			{
				TSharedPtr<FTCPClient> Client = ClientPair.Value;
				Client->Socket->Close();
				Clients.Remove(Client->Address);
				OnClientDisconnected.Broadcast(ClientAddress);
			}
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