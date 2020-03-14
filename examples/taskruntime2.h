#pragma once

#include <sys/types.h>
#include <sys/event.h>
#include <unistd.h>
#include "./../src/silk_pool.h"
    
namespace silk {
    namespace demo_runtime_2 {
        class task;
        
        struct uwcontext : public silk::wcontext {
        	bool is_recyclable;
        	task* continuation_task;
        	task* current_executable_task;
        };
        
        silk::wcontext* makeuwcontext() {
        	uwcontext* c = new uwcontext();
        	silk::init_wcontext(c);
        	c->current_executable_task = c->continuation_task = nullptr;
        	return c;
        }
        
        inline uwcontext* fetch_current_uwcontext() {
        	return (uwcontext*)silk::wcontexts[silk::current_worker_id];
        }
        
        class cancellation_token {
        	std::atomic<bool> is_cancelled_ = false;
        public:
        	bool is_cancelled(const std::memory_order memory_order = std::memory_order_acquire) const {
        		return is_cancelled_.load(memory_order);
        	}
        
        	void cancel(const std::memory_order memory_order = std::memory_order_release) {
        		is_cancelled_.store(true, memory_order);
        	}
        };
        
        class allocate_continuation_proxy {
        public:
        	task& allocate(size_t size) const;
        };
        
        class allocate_child_proxy {
        public:
        	task& allocate(size_t size) const;
        };
        
        class task : public silk::task {
        	task* continuation_;
        	std::atomic<int> ref_count_ = 0;
        	cancellation_token* cancellation_token_ = nullptr;
        public:
        	task() {
        		auto c = fetch_current_uwcontext();
        		continuation_ = c->continuation_task ? c->continuation_task : nullptr;
        		c->continuation_task = nullptr;
        	}
        
        	virtual ~task() {
        	}
        
        	allocate_continuation_proxy& allocate_continuation() {
        		return *reinterpret_cast<allocate_continuation_proxy*>(this);
        	}
        
        	allocate_child_proxy& allocate_child() {
        		return *reinterpret_cast<allocate_child_proxy*>(this);
        	}
        
        	void *operator new(const size_t bytes) {
        		return ::operator new(bytes);
        	}
        
        	void *operator new(const size_t bytes, const allocate_continuation_proxy& p) {
        		return &p.allocate(bytes);
        	}
        
        	void *operator new(const size_t bytes, const allocate_child_proxy& p) {
        		return &p.allocate(bytes);
        	}
        
        	virtual task* execute() = 0;
        
        	task* continuation() const {
        		return continuation_;
        	}
        
        	void set_continuation(task& t) {
        		continuation_ = &t;
        	}
        
        	void reset_continuation() {
        		continuation_ = nullptr;
        	}
        
        	void set_ref_count(const int count, const std::memory_order memory_order = std::memory_order_release) {
        		ref_count_.store(count, memory_order);
        	}
        
        	int ref_count(const std::memory_order memory_order = std::memory_order_acquire) const {
        		return ref_count_.load(memory_order);
        	}
        
        	int decrement_ref_count(const std::memory_order memory_order = std::memory_order_acquire) {
        		return ref_count_.fetch_sub(1, memory_order) - 1;
        	}
        
        	int increment_ref_count(const std::memory_order memory_order = std::memory_order_acquire) {
        		return ref_count_.fetch_add(1, memory_order) + 1;
        	}
        
        	bool is_canceled(const std::memory_order memory_order = std::memory_order_acquire) const {
        		return !cancellation_token_ ? false : cancellation_token_->is_cancelled(memory_order);
        	}
        
        	void cancel(const std::memory_order memory_order = std::memory_order_release) const {
        		if (cancellation_token_) {
        			cancellation_token_->cancel(memory_order);
        		}
        	}
        
        	void set_cancellation_token(cancellation_token* token) {
        		cancellation_token_ = token;
        	}
        
        	cancellation_token* cancellation_token() const {
        		return cancellation_token_;
        	}
        
        	static task* self() {
        		return fetch_current_uwcontext()->current_executable_task;
        	};
        
        protected:
        	static void recycle() {
        		fetch_current_uwcontext()->is_recyclable = true;
        	}
        
        	void recycle_as_child_of(task& t) {
        		continuation_ = &t;
        		recycle();
        	}
        };
        
        inline task& allocate_continuation_proxy::allocate(const size_t size) const {
        	task& t = *((task*)this);
        	fetch_current_uwcontext()->continuation_task = t.continuation();
        	t.reset_continuation();
        	return *((task*)::operator new(size));
        }
        
        inline task& allocate_child_proxy::allocate(const size_t size) const {
        	task& t = *((task*)this);
        	fetch_current_uwcontext()->continuation_task = &t;
        	return *((task*)::operator new(size));
        }
        
        void schedule(silk::task* v) {
        	uwcontext* cx = fetch_current_uwcontext();
        
        	task* t = (task*)v;
        
        	task* c = nullptr;
        
        	do {
        		if (!t->is_canceled()) {
        			cx->current_executable_task = t;
        			cx->is_recyclable = false;
        
        			task* bypass = t->execute();
        
        			cx->current_executable_task = nullptr;
        
        			if (!cx->is_recyclable && t->ref_count() == 0) {
        				if (t->continuation()) {
        					c = t->continuation();
        				}
        
        				delete t;
        			} else if (!bypass) {
        				break;
        			}
        
        			if (bypass) {
        				t = bypass;
        				continue;
        			}
        		} else {
        			c = t->continuation();
        			delete t;
        		}
        
        		t = c && c->decrement_ref_count() <= 0 ? c : nullptr;
        		c = nullptr;
        	} while (t);
        }
        
        inline void spawn(task& t) {
        	silk::spawn(silk::current_worker_id, (task*)&t);
        }
        
        int kq;
        
        typedef void(*readed_callback)(const int socket, char* buf, const int nbytes);
        
        class io_read_continuation : public task {
            readed_callback callback_;
            int read_sequence_count_;
            int nbytes_;
            int socket_;
            char* buf_;
        public:
            io_read_continuation(readed_callback callbak) : callback_(callbak) {
            }
                 
            void set_read_result(const int socket, char* buf, const int nbytes) {
                buf_ = buf;
                nbytes_ = nbytes;
                socket_ = socket;
                read_sequence_count_++;
            }
           
            int read_sequence_count() {
                return read_sequence_count_;
            }
                 
            task* execute() {
        		int read_sequence_count = read_sequence_count_;
        
                callback_( socket_, buf_, nbytes_ );
        		
        		if (read_sequence_count != read_sequence_count_) {
        			recycle();
        			
        			return this;
        		}
                         
                return nullptr;
            }
        };
        
        struct io_read_frame {
            io_read_continuation* continuation;
            int nbytes;
            char* buf;
        };
        
        void read_async(const int socket, char* buf, const int nbytes, const readed_callback callback) {
            uwcontext* c = fetch_current_uwcontext();
        
            io_read_continuation* t = dynamic_cast<io_read_continuation*>(c->current_executable_task);
        
            if (t && t->read_sequence_count() < 32) {
                memset(buf, 0, nbytes);
               
                int n = read(socket, buf, nbytes); //NON-BLOCKING MODE...
               
                if (n >= 0 || (n == -1 && errno != EAGAIN)) {
                    t->set_read_result(socket, buf, n);
                   
                    return;
                }
            }
        
            io_read_frame* frame = new io_read_frame();
            frame->continuation = new io_read_continuation(callback);
            frame->nbytes = nbytes;
            frame->buf = buf;
            
            struct kevent evSet;
            EV_SET(&evSet, socket, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, frame);
            assert(-1 != kevent(kq, & evSet, 1, NULL, 0, NULL));
        }
    }
}