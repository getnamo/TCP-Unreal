#pragma once

#include "Components/ActorComponent.h"
#include "Networking.h"
#include "Runtime/Sockets/Public/IPAddress.h"
#include "TCPComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCPEventSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCPMessageSignature, const TArray<uint8>&, Bytes);

UCLASS(ClassGroup = "Networking", meta = (BlueprintSpawnableComponent))
class TCPWRAPPER_API UTCPComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:

	//Async events

	/** On message received on the receiving socket. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPMessageSignature OnReceivedBytes;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnReceiveSocketStartedListening;

	/** Called after receiving socket has been closed. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnReceiveSocketStoppedListening;

	/** Default sending socket IP string in form e.g. 127.0.0.1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString SendIP;

	/** Default connection port e.g. 3001*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 SendPort;

	/** Default connection port e.g. 3002*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 ReceivePort;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString SendSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString ReceiveSocketName;

	/** in bytes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 BufferSize;

	/** If true will auto-connect on begin play to IP/port specified for sending TCP messages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bShouldAutoConnect;

	/** If true will auto-listen on begin play to port specified for receiving TCP messages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bShouldAutoListen;

	/** Whether we should process our data on the gamethread or the TCP thread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bReceiveDataOnGameThread;

	UPROPERTY(BlueprintReadOnly, Category = "TCP Connection Properties")
	bool bIsConnected;


	/**
	* Connect to a TCP endpoint, optional method if auto-connect is set to true.
	* Emit function will then work as long the network is reachable. By default
	* it will attempt this setup for this socket on beginplay.
	*
	* @param InIP the ip4 you wish to connect to
	* @param InPort the TCP port you wish to connect to
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void ConnectToSendSocket(const FString& InIP = TEXT("127.0.0.1"), const int32 InPort = 3000);

	/**
	* Close the sending socket. This is usually automatically done on endplay.
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void CloseSendSocket();

	/** 
	* Start listening at given port for TCP messages. Will auto-listen on begin play by default
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void StartReceiveSocketListening(const int32 InListenPort = 3002);

	/**
	* Close the receiving socket. This is usually automatically done on endplay.
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void CloseReceiveSocket();

	/**
	* Emit specified bytes to the TCP channel.
	*
	* @param Message	Bytes
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void Emit(const TArray<uint8>& Bytes);

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
protected:
	FSocket* SenderSocket;
	FSocket* ReceiverSocket;

	void OnDataReceivedDelegate(const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint);

	//FTCPSocketReceiver* TCPReceiver;
	FString SocketDescription;
	TSharedPtr<FInternetAddr> RemoteAdress;
	ISocketSubsystem* SocketSubsystem;
};