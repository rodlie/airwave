#include <stdio.h>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#include "common/logger.h"
#include "common/protocol.h"
#include "common/framequeue.h"
#include "common/dataport.h"
#include "common/event.h"
#include "common/storage.h"
#include "common/filesystem.h"
#include "common/moduleinfo.h"
#include "common/config.h"
#include "common/vst24.h"

using namespace Airwave;

namespace Airwave {

struct AirwaveTester {
	Airwave::DataPort controlPort_;
	DataPort audioPort_;
	FrameQueue audioCallback_;
	Event condition_;
	int childPid_;
	std::thread callbackThread_;
	Airwave::DataPort callbackPort_;
	AEffect* effect_;
	std::atomic_flag processCallbacks_;

	AirwaveTester(const std::string& vstPath, const std::string& hostPath,
		const std::string& prefixPath, const std::string& loaderPath,
		const std::string& logSocketPath) :
	  processCallbacks_(ATOMIC_FLAG_INIT) {
		
		// FIXME: frame size should be verified.
		if(!controlPort_.create(65536)) {
			printf("Unable to create control port\n");
			return;
		}

		// FIXME: frame size should be verified.
		if(!callbackPort_.create(FrameQueue::CALLBACK_FRAMESIZE)) {
			printf("Unable to create callback port\n");
			controlPort_.disconnect();
			return;
		}

		// Start the host endpoint's process.
		childPid_ = fork();
		if(childPid_ == -1) {
			printf("fork() call failed\n");
			controlPort_.disconnect();
			callbackPort_.disconnect();
			return;
		}
		else if(childPid_ == 0) {
			setenv("WINEPREFIX", prefixPath.c_str(), 1);
			setenv("WINELOADER", loaderPath.c_str(), 1);

			std::string id = std::to_string(controlPort_.id());
			std::string level = std::to_string(static_cast<int>(loggerLogLevel()));

			execl("/bin/sh", "/bin/sh", hostPath.c_str(), vstPath.c_str(), id.c_str(),
					level.c_str(), logSocketPath.c_str(), nullptr);

			// We should never reach this point on success child execution.
			printf("execl() call failed\n");
			return;
		}

		printf("Child process started, pid=%d\n", childPid_);

		processCallbacks_.test_and_set();
		callbackThread_ = std::thread(&AirwaveTester::callback, this);

		condition_.wait();

		// Send host info to the host endpoint.
		DataFrame* frame = controlPort_.frame<DataFrame>();
		frame->command = Command::HostInfo;
		frame->opcode = callbackPort_.id();
		controlPort_.sendRequest();

		printf("Waiting response from host endpoint...\n");

		// Wait for the host endpoint initialization.
		if(!controlPort_.waitResponse("Plugin::Plugin")) {
			printf("Host endpoint is not responding\n");
			kill(childPid_, SIGKILL);
			controlPort_.disconnect();
			callbackPort_.disconnect();
			childPid_ = -1;
			return;
		}

		audioCallback_.connect(controlPort_.id());

		PluginInfo* info = reinterpret_cast<PluginInfo*>(frame->data);
		effect_ = new AEffect;
		std::memset(effect_, 0, sizeof(AEffect));

		effect_->magic                  = kEffectMagic;
		effect_->object                 = this;
		effect_->__processDeprecated    = nullptr;
		effect_->flags                  = info->flags;
		effect_->numPrograms            = info->programCount;
		effect_->numParams              = info->paramCount;
		effect_->numInputs              = info->inputCount;
		effect_->numOutputs             = info->outputCount;
		effect_->initialDelay           = info->initialDelay;
		effect_->uniqueID               = info->uniqueId;
		effect_->version                = info->version;

		printf("VST plugin summary:\n");
		printf("  flags:         0x%08X\n", effect_->flags);
		printf("  program count: %d\n",     effect_->numPrograms);
		printf("  param count:   %d\n",     effect_->numParams);
		printf("  input count:   %d\n",     effect_->numInputs);
		printf("  output count:  %d\n",     effect_->numOutputs);
		printf("  initial delay: %d\n",     effect_->initialDelay);
		printf("  unique ID:     0x%08X\n", effect_->uniqueID);
		printf("  version:       %d\n",     effect_->version);
	}

	~AirwaveTester() {
		processCallbacks_.clear();
		if(callbackThread_.joinable())
			callbackThread_.join();

		controlPort_.disconnect();
		callbackPort_.disconnect();
		audioPort_.disconnect();

		int status;
		waitpid(childPid_, &status, 0);
		printf("Tester done for plugin.\n");		
	}

	intptr_t handleAudioMaster(DataFrame *frame) {
		printf("Plugin called API: handleAudioMaster -");
		switch(frame->opcode) {
		case audioMasterVersion:
			printf("audioMasterVersion\n");
			return 2400;
		default:
			printf("unknown event %s %d\n", kAudioMasterEvents[frame->opcode], frame->opcode);			
		}

		return 0;
	}

	void callback() {
		condition_.post();
		while(processCallbacks_.test_and_set()) {
			if(callbackPort_.waitRequest("Plugin::callbackThread", 100)) {
				DataFrame* frame = callbackPort_.frame<DataFrame>();
				frame->value = handleAudioMaster(frame);
				callbackPort_.sendResponse();
			}
		}
	}
};

}

int main(int argc, char *argv[]) {
	if (argc < 2 ) {
		printf("Usage: airwave-tester [path of linux airwave VST wrapper]\n");
		return 1;
	}

	Storage storage;
	std::string filePath = FileSystem::realPath(argv[1]);

	if(filePath.empty()) {
		printf("Unable to get an absolute path of the plugin binary\n");
		return 1;
	}

	Storage::Link link = storage.link(filePath);
	if(!link) {
		printf("Link '%s' is corrupted\n", filePath.c_str());
		return 1;
	}

	printf("Plugin binary: %s\n", filePath.c_str());

	std::string winePrefix = link.prefix();
	Storage::Prefix prefix = storage.prefix(winePrefix);
	if(!prefix) {
		printf("Invalid WINE prefix '%s'\n", winePrefix.c_str());
		return 1;
	}

	std::string prefixPath = FileSystem::realPath(prefix.path());
	if(!FileSystem::isDirExists(prefixPath)) {
		printf("WINE prefix directory '%s' doesn't exists\n", prefixPath.c_str());
		return 1;
	}

	printf("WINE prefix:   %s\n", prefixPath.c_str());

	std::string wineLoader = link.loader();
	Storage::Loader loader = storage.loader(wineLoader);
	if(!loader) {
		printf("Invalid WINE loader '%s'\n", wineLoader.c_str());
		return 1;
	}

	std::string loaderPath = FileSystem::realPath(loader.path());
	if(!FileSystem::isFileExists(loaderPath)) {
		printf("WINE loader binary '%s' doesn't exists\n", loaderPath.c_str());
		return 1;
	}	

	printf("WINE loader:   %s\n", loaderPath.c_str());

	std::string vstPath = prefixPath + '/' + link.target();
	if(!FileSystem::isFileExists(vstPath)) {
		printf("VST binary '%s' doesn't exists\n", vstPath.c_str());
		return 1;
	}

	printf("VST binary:    %s\n", vstPath.c_str());

	// Find host binary path
	ModuleInfo::Arch arch = ModuleInfo::instance()->getArch(vstPath);

	std::string hostName;
	if(arch == ModuleInfo::kArch64) {
		hostName = HOST_BASENAME "-64.exe";
	}
	else if(arch == ModuleInfo::kArch32) {
		hostName = HOST_BASENAME "-32.exe";
	}
	else {
		printf("Unable to determine VST plugin architecture\\n");
		return 1;
	}

	std::string hostPath = FileSystem::realPath(storage.binariesPath() + '/' + hostName);
	if(!FileSystem::isFileExists(hostPath)) {
		printf("Host binary '%s' doesn't exists\n", hostPath.c_str());
		return 1;
	}

	printf("Host binary:   %s\n", hostPath.c_str());

	AirwaveTester tester(vstPath, hostPath, prefixPath, loaderPath, storage.logSocketPath());
}
