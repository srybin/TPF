#include "IO.h"
#include "IoWorker.h"
#include "Scheduler.h"
#include "Sync.h"

using namespace Parallel;

IoWorker::IoWorker(IoQueue& ioQueue, Sync* sync) : Worker(sync), _ioQueue(ioQueue) {
}

void IoWorker::Execute() {
	CompletionContainer c;
	while (true) {
		if (_ioQueue.TryDequeue(c)) {
			Scheduler::Instance()->Spawn(c.Continuation, c.ThreadId);
		}
	}
}