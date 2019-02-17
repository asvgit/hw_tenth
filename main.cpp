#include <iostream>
#include <cassert>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <fstream>
#include <ctime>
#include <memory>
#include <thread>
#include <condition_variable>
#include <queue>

using string = std::string;
using StringVector = std::vector<string>;

class BulkManager {
public:
	class Observer : public std::enable_shared_from_this<Observer> {
	public:
		class UpdateHandler {
		protected:
			StringVector m_bulk;
		public:
			virtual ~UpdateHandler() = default;
			virtual void Update(Observer *, const string &) = 0;
		};


		Observer(const int size, UpdateHandler *uh) : m_update_handler(uh), max_size(size) {}
		virtual ~Observer() = default;
		std::shared_ptr<Observer> GetPtr() { return shared_from_this(); }
		void SetUpdadeHandler(UpdateHandler *uh) { m_update_handler.reset(uh); };
		int GetMaxSize() { return max_size; }
		virtual void PostBulk(const StringVector&) = 0;
		void Update(const string &msg) {
			if (m_update_handler != nullptr)
				m_update_handler->Update(this, msg);
		}
	private:
		std::unique_ptr<UpdateHandler> m_update_handler;
	protected:
		const int max_size;
	};
	using ObsPtr = std::shared_ptr<Observer>;

	void Subscribe(ObsPtr &obs) { m_subs.push_back(obs->GetPtr()); }

	void Listen() {
		for (string line; std::getline(std::cin, line);)
			Notify(line);
	}

private:
	std::vector<ObsPtr> m_subs;

	void Notify(const string &chunk) {
		for (const auto &s : m_subs)
			s->Update(chunk);
	};
};
using MgrPtr = std::shared_ptr<BulkManager>;

class DynamicHandler : public BulkManager::Observer::UpdateHandler {
	int m_count = 0;
	StringVector m_bulk;
	virtual void Update(BulkManager::Observer *, const string &) override;
};

class SizedHandler : public BulkManager::Observer::UpdateHandler {
	virtual void Update(BulkManager::Observer *o, const string &cmd) override {
		// auto &bulk = o->GetBulk();
		if (cmd == "{") {
			o->PostBulk(m_bulk);
			m_bulk.clear();
			o->SetUpdadeHandler(new DynamicHandler());
			return;
		}

		m_bulk.push_back(cmd);
		if (m_bulk.size() >= o->GetMaxSize()) {
			o->PostBulk(m_bulk);
			m_bulk.clear();
		}
	}
};

void DynamicHandler::Update(BulkManager::Observer *o, const string &cmd) {
	if (cmd == "{") {
		++m_count;
		return;
	}
	if (m_count && cmd == "}") {
		--m_count;
		return;
	}

	if (!m_count && cmd == "}") {
		o->PostBulk(m_bulk);
		m_bulk.clear();
		m_bulk.clear();
		o->SetUpdadeHandler(new SizedHandler());
	} else
		m_bulk.push_back(cmd);
}

class ConsoleOutput : public BulkManager::Observer {
public:
	ConsoleOutput(const int size) : Observer(size, new SizedHandler) {};

	void PostBulk(const StringVector &bulk) override {
		if (bulk.empty())
			return;
#ifdef NDEBUG
		sleep(1);
#endif
		for (auto &n : bulk) {
			std::cout << (&n == &bulk.front() ? "bulk: " : ", ")
					<< n;
		}
		std::cout << std::endl;
	}
};

// std::condition_variable m_cv;
// std::mutex m_cv_mutex;
// void worker(StringVector &q, bool &push_bulk, const bool &shutdown) {
//     while (true) {
//         std::unique_lock<std::mutex> lk(m_cv_mutex);
//         // std::cout << std::this_thread::get_id() << " waiting... " << std::endl;
//         m_cv.wait(lk, [&](){
//                 return (!q.empty() && push_bulk) || shutdown;
//         });
//         push_bulk = false;
//         if (shutdown && q.empty())
//             return;
//         // auto m = q.front();
//         // q.pop();
//         // lk.unlock();
//
//         // std::cout << std::this_thread::get_id() << " " << q.size() << " pop " << m << std::endl;
//
//         for (const auto &item : q) {
//             std::cout << std::this_thread::get_id() << " " << item << std::endl;
//         }
//         q.clear();
//     }
// }
//
// class FileOutput : public BulkManager::Observer {
//     int cmd_time;
//     bool push_bulk = false;
//     bool shutdown = false;
//
//     std::thread m_thread1;
//     std::thread m_thread2;
//
// public:
//     FileOutput(const int size) : Observer(size, new SizedHandler)
//         , m_thread1(worker, std::ref(m_bulk), std::ref(push_bulk), std::ref(shutdown))
//         , m_thread2(worker, std::ref(m_bulk), std::ref(push_bulk), std::ref(shutdown))
//     {};
//     ~FileOutput() {
//         shutdown = true;
//         m_cv.notify_all();
//         m_thread1.join();
//         m_thread2.join();
//     }
//
//     void PostBulk() override {
//         if (m_bulk.empty())
//             return;
//
//         // std::ofstream file("bulk" + std::to_string(std::time(0)) + ".log");
//         // for (auto &n : m_bulk)
//         //     file << n << std::endl;
//
//         {
//             std::lock_guard<std::mutex> lk(m_cv_mutex);
//             push_bulk = true;
//             // msgs.push("cmd1");
//             // msgs.push("cmd2");
//         }
//         m_cv.notify_one();
//     }
// };

int main(int argc, char *argv[]) {
	try {
		assert(argc == 2);
		const int bulk_size = [&]() {
			std::stringstream ss(argv[1]);
			int n;
			ss >> n;
			return n;
		}();

		BulkManager::ObsPtr co(new ConsoleOutput(bulk_size));
		// BulkManager::ObsPtr fo(new FileOutput(bulk_size));

		MgrPtr bulk_mgr(new BulkManager());
		bulk_mgr->Subscribe(co);
		// bulk_mgr->Subscribe(fo);
		bulk_mgr->Listen();
	} catch(const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	return 0;
}
