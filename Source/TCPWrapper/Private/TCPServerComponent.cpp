
#include "TCPServerComponent.h"
#include "Async.h"
#include "TCPWrapperUtility.h"
#include "Runtime/Sockets/Public/SocketSubsystem.h"
#include "Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h"

UTCPServerComponent::UTCPServerComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoListen = true;
	bReceiveDataOnGameThread = true;
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	ListenPort = 3000;
	ListenSocketName = FString(TEXT("ue4-tcp-server"));

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

				Clients.Add(Client);	//todo: balance this with remove when clients disconnect

				AsyncTask(ENamedThreads::GameThread, [&, AddressString]()
				{
					OnClientConnected.Broadcast(AddressString);
				});
			}

			//Check each endpoint for data
			for (FSocket* Client : Clients)
			{
				if (Client->HasPendingData(BufferSize))
				{
					ReceiveBuffer.SetNumUninitialized(BufferSize);

					int32 Read = 0;
					Client->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), Read);

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
			}

			//sleep for 10microns
			FPlatformProcess::Sleep(0.00001);
		}//end while

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
		//Gracefully close connections
		for (FSocket* Client : Clients)
		{
			Client->Close();
		}

		bShouldListen = false;
		ServerFinishedFuture.Get();

		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;

		OnListenEnd.Broadcast();
	}
}

void UTCPServerComponent::Emit(const TArray<uint8>& Bytes, const FString& ToClient)
{
	if (Clients.Num()>0)
	{
		int32 BytesSent = 0;
		//simple multi-cast
		if (ToClient == TEXT("All"))
		{
			for (FSocket* Client : Clients)
			{
				Client->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
			}
		}
		//match client address and port
		else
		{
			TSharedPtr<FInternetAddr> Addr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
			for (FSocket* Client : Clients)
			{
				Client->GetAddress(*Addr);
				if (Addr->ToString(true) == ToClient)
				{
					Client->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
				}
			}
		}
	}
}

void UTCPServerComponent::InitializeComponent()
{
	Super::InitializeComponent();
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