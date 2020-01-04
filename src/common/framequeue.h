#pragma once

#include <sys/ipc.h>
#include <sys/msg.h>

#include "common/types.h"
#include "common/protocol.h"
#include "common/logger.h"
#include <string.h>

namespace Airwave {

class FrameQueue {
    int msgid_;
public:
    FrameQueue() {
        msgid_ = -1;
    }

    ~FrameQueue() {
        if ( msgid_ > 0 )
            msgctl(msgid_, IPC_RMID, 0);
    }

    bool connect(int id) {
        msgid_ = msgget(id, 0600 | IPC_CREAT);
        if (msgid_ == -1) {
            ERROR("Unable to connect FrameQueue port (id = %d)", id);
            return false;
		}
        return true;
    }

    void pushFrame(DataFrame *frame) {
        Message msg;
        memcpy(&(msg.data), frame, CALLBACK_FRAMESIZE);
        if ( msgsnd(msgid_, &msg, CALLBACK_FRAMESIZE, IPC_NOWAIT) == -1 ) {
            ERROR("Error sending message %s", strerror(errno));
        }
    }

    bool popFrame(DataFrame *frame) {
        if ( msgrcv(msgid_, frame, CALLBACK_FRAMESIZE, 0, IPC_NOWAIT) == -1 )
            return false;
        return true;
    }
    // This is standard Linux msg size from /proc/sys/kernel/msgmax
    static const int CALLBACK_FRAMESIZE = 8192;
private:
    struct Message {
        Message() { mtype = 1; }
        long mtype;
        u8   data[FrameQueue::CALLBACK_FRAMESIZE];
    };
};

}