
#include "TCPClientComponent.h"
#include "Async.h"
#include "TCPWrapperUtility.h"
#include "Runtime/Sockets/Public/SocketSubsystem.h"
#include "Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h"

TFuture<void> RunLambdaOnBackGroundThread(TFunction< void()> InFunction)
{
	return Async(EAsyncExecution::Thread, InFunction);
}

UTCPClientComponent::UTCPClientComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoConnectOnBeginPlay = false;
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
	//ClientSocket->Listen(8);

	bIsConnected = ClientSocket->Connect(*RemoteAdress);

	if (bIsConnected)
	{
		OnConnected.Broadcast();
	}
	bShouldContinueListening = true;

	//Listen for data on our end
	ClientConnectionFinishedFuture = FTCPWrapperUtility::RunLambdaOnBackGroundThread([&]()
	{
		uint32 BufferSize = 0;
		TArray<uint8> ReceiveBuffer;
		while (bShouldContinueListening)
		{
			bool bHasPendingConnection;
			ClientSocket->HasPendingConnection(bHasPendingConnection);
			if (bHasPendingConnection) 
			{
				UE_LOG(LogTemp, Log, TEXT("has waiting connection"));
			}

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
			//sleep 0.1ms
			FPlatformProcess::Sleep(0.0001);
		}
	});
}

void UTCPClientComponent::CloseSocket()
{
	if (ClientSocket)
	{
		bShouldContinueListening = false;
		ClientConnectionFinishedFuture.Get();

		ClientSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
		ClientSocket = nullptr;
	}
}

void UTCPClientComponent::Emit(const TArray<uint8>& Bytes)
{
	if (ClientSocket && ClientSocket->GetConnectionState() == SCS_Connected)
	{
		int32 BytesSent = 0;
		ClientSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
	}
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

void UTCPClientComponent::OnDataReceivedDelegate(const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint)
{
	TArray<uint8> Data;
	Data.AddUninitialized(DataPtr->TotalSize());
	DataPtr->Serialize(Data.GetData(), DataPtr->TotalSize());

	if (bReceiveDataOnGameThread)
	{
		//Pass the reference to be used on gamethread
		AsyncTask(ENamedThreads::GameThread, [&, Data]()
		{
			OnReceivedBytes.Broadcast(Data);
		});
	}
	else
	{
		OnReceivedBytes.Broadcast(Data);
	}
}
