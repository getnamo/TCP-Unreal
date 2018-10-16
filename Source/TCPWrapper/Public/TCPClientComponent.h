#pragma once

#include "Components/ActorComponent.h"
#include "Networking.h"
#include "Runtime/Sockets/Public/IPAddress.h"
#include "TCPServerComponent.h"
#include "TCPClientComponent.generated.h"


UCLASS(ClassGroup = "Networking", meta = (BlueprintSpawnableComponent))
class TCPWRAPPER_API UTCPClientComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:

	//Async events

	/** On message received on the receiving socket. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPMessageSignature OnReceivedBytes;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnListenServerStarted;

	/** Called after receiving socket has been closed. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnListenServerStopped;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnClientConnectedToListenServer;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnConnectedToClientSocket;

	/** Default sending socket IP string in form e.g. 127.0.0.1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString ClientIP;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 ClientPort;

	/** Default connection port e.g. 3001*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 ListenPort;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString ClientSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString ListenSocketName;

	/** in bytes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 BufferMaxSize;

	/** If true will auto-connect on begin play to IP/port specified as a client. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bShouldAutoConnectAsClient;

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
	void ConnectToSocketAsClient(const FString& InIP = TEXT("127.0.0.1"), const int32 InPort = 3000);

	/**
	* Close the sending socket. This is usually automatically done on endplay.
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void CloseClientSocket();

	/** 
	* Start listening at given port for TCP messages. Will auto-listen on begin play by default
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void StartListenServer(const int32 InListenPort = 3001);

	/**
	* Close the receiving socket. This is usually automatically done on endplay.
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void CloseListenServer();

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
	FSocket* ClientSocket;
	FSocket* ListenSocket;
	FThreadSafeBool bShouldContinueListening;
	TFuture<void> ListenServerStoppedFuture;

	void OnDataReceivedDelegate(const FArrayReaderPtr& DataPtr, const FIPv4Endpoint& Endpoint);

	//FTCPSocketReceiver* TCPReceiver;
	FString SocketDescription;
	TSharedPtr<FInternetAddr> RemoteAdress;
	ISocketSubsystem* SocketSubsystem;
};