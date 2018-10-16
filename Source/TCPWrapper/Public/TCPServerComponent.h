#pragma once

#include "Components/ActorComponent.h"
#include "Networking.h"
#include "Runtime/Sockets/Public/IPAddress.h"
#include "TCPServerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCPEventSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCPMessageSignature, const TArray<uint8>&, Bytes);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCPClientSignature, const FString&, Client);

UCLASS(ClassGroup = "Networking", meta = (BlueprintSpawnableComponent))
class TCPWRAPPER_API UTCPServerComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:

	//Async events

	/** On message received on the receiving socket. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPMessageSignature OnReceivedBytes;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnListenBegin;

	/** Called after receiving socket has been closed. */
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPEventSignature OnListenEnd;

	/** Callback when we start listening on the TCP receive socket*/
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPClientSignature OnClientConnected;

	/** Default connection port e.g. 3001*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 ListenPort;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString ListenSocketName;

	/** in bytes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	int32 BufferMaxSize;

	/** If true will auto-listen on begin play to port specified for receiving TCP messages. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bShouldAutoListen;

	/** Whether we should process our data on the game thread or the TCP thread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bReceiveDataOnGameThread;

	UPROPERTY(BlueprintReadOnly, Category = "TCP Connection Properties")
	bool bIsConnected;

	/** 
	* Start listening at given port for TCP messages. Will auto-listen on begin play by default
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void StartListenServer(const int32 InListenPort = 3001);

	/**
	* Close the receiving socket. This is usually automatically done on end play.
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void StopListenServer();

	/**
	* Emit specified bytes to the TCP channel.
	*
	* @param Message	Bytes
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void Emit(const TArray<uint8>& Bytes, const FString& ToClient = TEXT("All"));

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
protected:
	FSocket* ListenSocket;
	FThreadSafeBool bShouldListen;
	TFuture<void> ListenServerStoppedFuture;

	FString SocketDescription;
	TSharedPtr<FInternetAddr> RemoteAdress;
};