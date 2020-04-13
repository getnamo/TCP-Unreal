
#include "TCPClientComponent.h"
#include "Async/Async.h"
#include "TCPWrapperUtility.h"
#include "SocketSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"

TFuture<void> RunLambdaOnBackGroundThread(TFunction< void()> InFunction)
{
	return Async(EAsyncExecution::Thread, InFunction);
}

UTCPClientComponent::UTCPClientComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoConnectOnBeginPlay = true;
	bReceiveDataOnGameThread = true;
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	ConnectionIP = FString(TEXT("127.0.0.1"));
	ConnectionPort = 3000;
	ClientSocketName = FString(TEXT("ue4-tcp-client"));
	ClientSocket = nullptr;

	BufferMaxSize = 2 * 1024 * 1024;	//default roughly 2mb
}

void UTCPClientComponent::ConnectToSocketAsClient(const FString& InIP /*= TEXT("127.0.0.1")*/, const int32 InPort /*= 3000*/)
{
	RemoteAdress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	
	bool bIsValid;
	RemoteAdress->SetIp(*InIP, bIsValid);
	RemoteAdress->SetPort(InPort);

	if (!bIsValid)
	{
		UE_LOG(LogTemp, Error, TEXT("TCP address is invalid <%s:%d>"), *InIP, InPort);
		return ;
	}

	ClientSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, ClientSocketName, false);

	//Set Send Buffer Size
	ClientSocket->SetSendBufferSize(BufferMaxSize, BufferMaxSize);
	ClientSocket->SetReceiveBufferSize(BufferMaxSize, BufferMaxSize);

	bIsConnected = ClientSocket->Connect(*RemoteAdress);

	if (bIsConnected)
	{
		OnConnected.Broadcast();
	}
	bShouldReceiveData = true;

	//Listen for data on our end
	ClientConnectionFinishedFuture = FTCPWrapperUtility::RunLambdaOnBackGroundThread([&]()
	{
		uint32 BufferSize = 0;
		TArray<uint8> ReceiveBuffer;
		while (bShouldReceiveData)
		{
			if (ClientSocket->HasPendingData(BufferSize))
			{
				ReceiveBuffer.SetNumUninitialized(BufferSize);

				int32 Read = 0;
				ClientSocket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), Read);

				if (bReceiveDataOnGameThread)
				{
					//Copy buffer so it's still valid on game thread
					TArray<uint8> ReceiveBufferGT;
					ReceiveBufferGT.Append(ReceiveBuffer);

					//Pass the reference to be used on game thread
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
			//sleep until there is data or 10 ticks
			ClientSocket->Wait(ESocketWaitConditions::WaitForReadOrWrite, FTimespan(1));
		}
	});
}

void UTCPClientComponent::CloseSocket()
{
	if (ClientSocket)
	{
		bShouldReceiveData = false;
		ClientConnectionFinishedFuture.Get();

		ClientSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
		ClientSocket = nullptr;
	}
}

bool UTCPClientComponent::Emit(const TArray<uint8>& Bytes)
{
	if (ClientSocket && ClientSocket->GetConnectionState() == SCS_Connected)
	{
		int32 BytesSent = 0;
		return ClientSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
	}
	return false;
}

void UTCPClientComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UTCPClientComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UTCPClientComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShouldAutoConnectOnBeginPlay)
	{
		ConnectToSocketAsClient(ConnectionIP, ConnectionPort);
	}
}

void UTCPClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseSocket();

	Super::EndPlay(EndPlayReason);
}