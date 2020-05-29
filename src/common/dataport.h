#ifndef COMMON_DATAPORT_H
#define COMMON_DATAPORT_H

#include "common/event.h"
#include "common/types.h"


namespace Airwave {


class DataPort {
public:
	DataPort();
	~DataPort();

	bool create(size_t frameSize);
	bool connect(int id);
	void disconnect();

	bool isNull() const;
	bool isConnected() const;
	int id() const;
	size_t frameSize() const;

	void* frameBuffer();

	template<typename T>
	T* frame();

	void sendRequest();
	void sendResponse();

	bool waitRequest(const char *debugObject, int msecs = -1);
	bool waitResponse(const char *debugObject, int msecs = -1);

private:
	struct ControlBlock {
		Event request;
		Event response;
	};

	int id_;
	size_t frameSize_;
	void* buffer_;

	ControlBlock* controlBlock();

	// if the waitRequest or waitResponse is asking for -1; we impose a soft limit to avoid host lock on 
	// dsp thread
	int wait_softlimit;
};


template<typename T>
T* DataPort::frame()
{
	return static_cast<T*>(frameBuffer());
}


} // namespace Airwave


#endif // COMMON_DATAPORT_H
