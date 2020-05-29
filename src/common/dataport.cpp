#include "dataport.h"

#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include "common/logger.h"

namespace Airwave {


DataPort::DataPort() :
	id_(-1),
	frameSize_(0),
	buffer_(nullptr)
{
	wait_softlimit = 30000;
}


DataPort::~DataPort()
{
	disconnect();
}


bool DataPort::create(size_t frameSize)
{
	if(!isNull()) {
		ERROR("Unable to create, port is already created");
		return false;
	}

	size_t bufferSize = sizeof(ControlBlock) + frameSize;

	id_ = shmget(IPC_PRIVATE, bufferSize, S_IRUSR | S_IWUSR);
	if(id_ < 0) {
		ERROR("Unable to allocate %d bytes of shared memory", bufferSize);
		return false;
	}

	buffer_ = shmat(id_, nullptr, 0);
	if(buffer_ == reinterpret_cast<void*>(-1)) {
		ERROR("Unable to attach shared memory segment with id %d", id_);
		shmctl(id_, IPC_RMID, nullptr);
		id_ = -1;
		return false;
	}

	new (controlBlock()) ControlBlock;

	frameSize_ = frameSize;
	return true;
}


bool DataPort::connect(int id)
{
	if(!isNull()) {
		ERROR("Unable to connect on already initialized port");
		return false;
	}

	buffer_ = shmat(id, nullptr, 0);
	if(buffer_ == reinterpret_cast<void*>(-1)) {
		ERROR("Unable to attach shared memory segment with id %d", id);
		return false;
	}

	shmid_ds info;
	if(shmctl(id, IPC_STAT, &info) != 0) {
		ERROR("Unable to get info about shared memory segment with id %d", id);
		shmdt(buffer_);
		id_ = -1;
		return false;
	}

	size_t bufferSize = info.shm_segsz;
	frameSize_ = bufferSize - sizeof(ControlBlock);

	id_ = id;
	return true;
}


void DataPort::disconnect()
{
	if(!isNull()) {
		if(!isConnected()) {
//			ControlBlock* control = controlBlock();
//			sem_destroy(&control->request);
//			sem_destroy(&control->response);
		}

		shmdt(buffer_);
		shmctl(id_, IPC_RMID, nullptr);
		id_ = -1;
		buffer_ = nullptr;
		frameSize_ = 0;
	}
}


bool DataPort::isNull() const
{
	return id_ < 0;
}


bool DataPort::isConnected() const
{
	shmid_ds info;

	if(shmctl(id_, IPC_STAT, &info) != 0) {
		ERROR("Unable to get shared memory segment (%d) info", id_);
		return false;
	}

	return info.shm_nattch > 1;
}


int DataPort::id() const
{
	return id_;
}


size_t DataPort::frameSize() const
{
	return frameSize_;
}


void* DataPort::frameBuffer()
{
	return controlBlock() + 1;
}


void DataPort::sendRequest()
{
	if(!isNull())
		controlBlock()->request.post();
}


void DataPort::sendResponse()
{
	if(!isNull())
		controlBlock()->response.post();
}


bool DataPort::waitRequest(const char *debugObject, int msecs)
{
	if ( msecs == -1 ) 
	{
		if ( !controlBlock()->request.wait(wait_softlimit) ) 
		{
			ERROR("waitRequest FAILED for %s", debugObject);
			return false;
		}
		return true;
	} else {
		return controlBlock()->request.wait(msecs);
	}
}


bool DataPort::waitResponse(const char *debugObject, int msecs)
{
	if ( msecs == -1 ) 
	{
		if ( !controlBlock()->response.wait(wait_softlimit) ) 
		{
			ERROR("waitResponse FAILED for %s", debugObject);
			return false;
		}
		return true;
	} else {
		return controlBlock()->response.wait(msecs);
	}
}


DataPort::ControlBlock* DataPort::controlBlock()
{
	return static_cast<ControlBlock*>(buffer_);
}


} // namespace Airwave
