
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
	FIPv4Address Addr;
	FIPv4Address::Parse(TEXT("0.0.0.0"), Addr);

	//Create Socket
	FIPv4Endpoint Endpoint(Addr, InListenPort);

	ListenSocket = FTcpSocketBuilder(*ListenSocketName)
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferMaxSize);

	ListenSocket->SetReceiveBufferSize(BufferMaxSize, BufferMaxSize);
	ListenSocket->SetSendBufferSize(BufferMaxSize, BufferMaxSize);

	ListenSocket->Listen(8);

	OnListenBegin.Broadcast();

	//Start a lambda thread to handle data
	FTCPWrapperUtility::RunLambdaOnBackGroundThread([&]()
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
				FString Description = ListenSocket->GetDescription();
				ListenSocket->Accept(Description);
			}

			if (ListenSocket->HasPendingData(BufferSize))
			{
				ReceiveBuffer.SetNumUninitialized(BufferSize);

				int32 Read = 0;
				ListenSocket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), Read);

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

				//sleep until there is data or 10 ticks
				ListenSocket->Wait(ESocketWaitConditions::WaitForReadOrWrite, FTimespan(10));
			}
		}
	});
}

void UTCPServerComponent::StopListenServer()
{
	if (ListenSocket)
	{
		bShouldListen = false;
		ListenServerStoppedFuture.Get();

		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;

		OnListenEnd.Broadcast();
	}
}

void UTCPServerComponent::Emit(const TArray<uint8>& Bytes, const FString& ToClient)
{
	if (ListenSocket->GetConnectionState() == SCS_Connected)
	{
		int32 BytesSent = 0;
		ListenSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
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