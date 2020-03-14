#include <iostream>
#include "taskruntime2.h"

long serial_fib(const long n) {
	if (n < 2)
		return n;
	
	return serial_fib(n - 1) + serial_fib(n - 2);
}

struct FibContinuation : public silk::demo_runtime_2::task {
	long* const sum;
	long x, y;
	FibContinuation(long* sum_) : sum(sum_) {
	}
	task* execute() override {
		*sum = x + y;
		return nullptr;
	}
};

struct FibTask : public silk::demo_runtime_2::task {
	long n;
	long* sum;
	FibTask(long n_, long* sum_) : n(n_), sum(sum_) {
	}
	task* execute() override {
		if (n<14) {
			*sum = serial_fib(n);
			return nullptr;
		} else {
			FibContinuation& c = *new(allocate_continuation()) FibContinuation(sum);
			FibTask& a = *new(c.allocate_child()) FibTask(n - 2, &c.x);
			n = n - 1;
			sum = &c.y;
			recycle_as_child_of(c);
			c.set_ref_count(2);
			spawn(a);
			return this;
		}
	}
};

int main() {
    silk::init_pool(silk::demo_runtime_2::schedule, silk::demo_runtime_2::makeuwcontext);

    const auto start = std::chrono::high_resolution_clock::now();
    
    long sum;
    silk::demo_runtime_2::spawn( *new FibTask(45, &sum) );
    
    silk::join_main_thread_2_pool(silk::demo_runtime_2::schedule);
    
    const auto end = std::chrono::high_resolution_clock::now();

    std::cout << "result: " << sum << " time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << std::endl;

    return 0;
}