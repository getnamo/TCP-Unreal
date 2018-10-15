
#include "TCPComponent.h"
#include "Async.h"
#include "Runtime/Sockets/Public/SocketSubsystem.h"
#include "Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h"

UTCPComponent::UTCPComponent(const FObjectInitializer &init) : UActorComponent(init)
{
	bShouldAutoConnect = true;
	bShouldAutoListen = true;
	bReceiveDataOnGameThread = true;
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SendIP = FString(TEXT("127.0.0.1"));
	SendPort = 3001;
	ReceivePort = 3002;
	SendSocketName = FString(TEXT("ue4-dgram-send"));
	ReceiveSocketName = FString(TEXT("ue4-dgram-receive"));

	BufferSize = 2 * 1024 * 1024;	//default roughly 2mb
}

void UTCPComponent::ConnectToSendSocket(const FString& InIP /*= TEXT("127.0.0.1")*/, const int32 InPort /*= 3000*/)
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

	/*SenderSocket = FTCPSocketBuilder(*SendSocketName).AsReusable().WithBroadcast();

	//check(SenderSocket->GetSocketType() == SOCKTYPE_Datagram);

	//Set Send Buffer Size
	SenderSocket->SetSendBufferSize(BufferSize, BufferSize);
	SenderSocket->SetReceiveBufferSize(BufferSize, BufferSize);

	bool bDidConnect = SenderSocket->Connect(*RemoteAdress);*/
}

void UTCPComponent::StartReceiveSocketListening(const int32 InListenPort /*= 3002*/)
{
	FIPv4Address Addr;
	FIPv4Address::Parse(TEXT("0.0.0.0"), Addr);

	//Create Socket
	FIPv4Endpoint Endpoint(Addr, InListenPort);

	/*ReceiverSocket = FTCPSocketBuilder(*ReceiveSocketName)
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithReceiveBufferSize(BufferSize);

	FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);
	FString ThreadName = FString::Printf(TEXT("TCP RECEIVER-%s"), *UKismetSystemLibrary::GetDisplayName(this));
	/*TCPReceiver = new FTCPSocketReceiver(ReceiverSocket, ThreadWaitTime, *ThreadName);

	TCPReceiver->OnDataReceived().BindUObject(this, &UTCPComponent::OnDataReceivedDelegate);
	OnReceiveSocketStartedListening.Broadcast();

	TCPReceiver->Start();*/
}

void UTCPComponent::CloseReceiveSocket()
{
	/*if (ReceiverSocket)
	{
		TCPReceiver->Stop();
		delete TCPReceiver;
		TCPReceiver = nullptr;

		ReceiverSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ReceiverSocket);
		ReceiverSocket = nullptr;

		OnReceiveSocketStoppedListening.Broadcast();
	}*/
}

void UTCPComponent::CloseSendSocket()
{
	/*if (SenderSocket)
	{
		SenderSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SenderSocket);
		SenderSocket = nullptr;
	}*/
}

void UTCPComponent::Emit(const TArray<uint8>& Bytes)
{
	if (SenderSocket->GetConnectionState() == SCS_Connected)
	{
		int32 BytesSent = 0;
		SenderSocket->Send(Bytes.GetData(), Bytes.Num(), BytesSent);
	}
}

void UTCPComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UTCPComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
}

void UTCPComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bShouldAutoListen)
	{
		StartReceiveSocketListening(ReceivePort);
	}
	if (bShouldAutoConnect)
	{
		ConnectToSendSocket(SendIP, SendPort);
	}
}

void UTCPComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseSendSocket();
	CloseReceiveSocket();

	Super::EndPlay(EndPlayReason);
}

void UTCPComponent::OnDataReceivedDelegate(const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint)
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
