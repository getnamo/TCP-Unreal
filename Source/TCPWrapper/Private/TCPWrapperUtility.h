#pragma once

class FTCPWrapperUtility
{
public:
	static TFuture<void> RunLambdaOnBackGroundThread(TFunction< void()> InFunction)
	{
		return Async(EAsyncExecution::Thread, InFunction);
	}

	static TFuture<void> RunLambdaOnGameThread(TFunction< void()> InFunction)
	{
		return Async(EAsyncExecution::TaskGraphMainThread, InFunction);
	}
};