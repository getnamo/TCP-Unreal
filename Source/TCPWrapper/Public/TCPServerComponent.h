#pragma once

#include "Components/ActorComponent.h"
#include "Networking.h"
#include "IPAddress.h"
#include "TCPServerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTCPEventSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCPMessageSignature, const TArray<uint8>&, Bytes);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTCPClientSignature, const FString&, Client);

struct FTCPClient
{
	FSocket* Socket;
	FString Address;

	bool operator==(const FTCPClient& Other)
	{
		return Address == Other.Address;
	}
};

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

	//This will only get called if an emit fails due to how FSocket works. Use custom disconnect logic if possible.
	UPROPERTY(BlueprintAssignable, Category = "TCP Events")
	FTCPClientSignature OnClientDisconnected;

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

	/** With current FSocket architecture, this is about the only way to catch a disconnection*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bDisconnectOnFailedEmit;

	/** Convenience ping send utility used to determine if connection disconnected. This is a custom system and not a normal ping*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	bool bShouldPing;

	/** How often we should ping */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	float PingInterval;

	/** What the default ping message should be*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TCP Connection Properties")
	FString PingMessage;

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
	* @param ToClient	Client Address and port, obtained from connection event or 'All' for multicast
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	bool Emit(const TArray<uint8>& Bytes, const FString& ToClient = TEXT("All"));

	/** 
	* Disconnects client on the next tick
	* @param ClientAddress	Client Address and port, obtained from connection event or 'All' for multicast
	*/
	UFUNCTION(BlueprintCallable, Category = "TCP Functions")
	void DisconnectClient(FString ClientAddress = TEXT("All"), bool bDisconnectNextTick = false);

	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
protected:
	TMap<FString, TSharedPtr<FTCPClient>> Clients;
	FSocket* ListenSocket;
	FThreadSafeBool bShouldListen;
	TFuture<void> ServerFinishedFuture;
	TArray<uint8> PingData;

	FString SocketDescription;
	TSharedPtr<FInternetAddr> RemoteAdress;
};