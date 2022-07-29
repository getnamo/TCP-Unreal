
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
	bAutoDisconnectOnSendFailure = true;
	bAutoReconnectOnSendFailure = true;
	ConnectionIP = FString(TEXT("127.0.0.1"));
	ConnectionPort = 3000;
	ClientSocketName = FString(TEXT("unreal-tcp-client"));
	ClientSocket = nullptr;

	BufferMaxSize = 2 * 1024 * 1024;	//default roughly 2mb
}

void UTCPClientComponent::ConnectToSocketAsClient(const FString& InIP /*= TEXT("127.0.0.1")*/, const int32 InPort /*= 3000*/)
{
	//Already connected? attempt reconnect
	if (IsConnected())
	{
		CloseSocket();
		ConnectToSocketAsClient(InIP, InPort);
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	if (SocketSubsystem == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("TCPClientComponent: SocketSubsystem is nullptr"));
        return;
    }

	auto ResolveInfo = SocketSubsystem->GetHostByName(TCHAR_TO_ANSI(*InIP));
	while (!ResolveInfo->IsComplete());

	auto error = ResolveInfo->GetErrorCode();

	if (error != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("TCPClientComponent: DNS resolve error code %d"), error);
        return;
    }

	RemoteAdress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	RemoteAdress->SetRawIp(ResolveInfo->GetResolvedAddress().GetRawIp()); // todo: somewhat wasteful, we could probably use the same address object?
	RemoteAdress->SetPort(InPort);

	ClientSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, ClientSocketName, false);

	//Set Send Buffer Size
	ClientSocket->SetSendBufferSize(BufferMaxSize, BufferMaxSize);
	ClientSocket->SetReceiveBufferSize(BufferMaxSize, BufferMaxSize);

	//Listen for data on our end
	ClientConnectionFinishedFuture = FTCPWrapperUtility::RunLambdaOnBackGroundThread([&]()
	{
		double LastConnectionCheck = FPlatformTime::Seconds();

		uint32 BufferSize = 0;
		TArray<uint8> ReceiveBuffer;
		bShouldAttemptConnection = true;

		while (bShouldAttemptConnection)
		{
			if (ClientSocket->Connect(*RemoteAdress))
			{
				FTCPWrapperUtility::RunLambdaOnGameThread([&]()
				{
					OnConnected.Broadcast();
				});
				bShouldAttemptConnection = false;
				continue;
			}
		
			//reconnect attempt every 3 sec
			FPlatformProcess::Sleep(3.f);
		}

		bShouldReceiveData = true;

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
			//sleep until there is data or 10 ticks (0.1micro seconds
			ClientSocket->Wait(ESocketWaitConditions::WaitForReadOrWrite, FTimespan(10));

			//Check every second if we're still connected
			//NB: this doesn't really work atm, disconnects are not captured on receive pipe
			//detectable on send failure though
			/*double Now = FPlatformTime::Seconds();
			if (Now > (LastConnectionCheck + 1.0)) 
			{
				LastConnectionCheck = Now;
				if (!IsConnected())
				{
					bShouldReceiveData = false;
					FTCPWrapperUtility::RunLambdaOnGameThread([&]() 
					{
						OnDisconnected.Broadcast();
					});
					
				}
			}*/
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

		OnDisconnected.Broadcast();
	}
}

bool UTCPClientComponent::Emit(const TArray<uint8>& Bytes)
{
	if (IsConnected())
	{
		int32 BytesSent = 0;
		bool bDidSend = ClientSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
		

		//If we're supposedly connected but failed to send
		if (IsConnected() && !bDidSend)
		{
			UE_LOG(LogTemp, Warning, TEXT("Sending Failure detected"));

			if (bAutoDisconnectOnSendFailure)
			{
				UE_LOG(LogTemp, Warning, TEXT("disconnecting socket."));
				CloseSocket();
			}

			if (bAutoReconnectOnSendFailure)
			{
				UE_LOG(LogTemp, Warning, TEXT("reconnecting..."));
				ConnectToSocketAsClient(ConnectionIP, ConnectionPort);
			}
		}
		return bDidSend;
	}
	return false;
}

bool UTCPClientComponent::IsConnected()
{
	return (ClientSocket && (ClientSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected));
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
