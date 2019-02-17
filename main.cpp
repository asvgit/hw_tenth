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

struct ThreadStat {
	string name;
	int bulk_count = 0;
	int cmd_count = 0;
	ThreadStat(const string &n) : name(n) {}
};

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
		size_t GetMaxSize() { return max_size; }
		virtual void PostBulk(StringVector&) = 0;
		virtual void PrintStat() {};
		void Update(const string &msg) {
			if (m_update_handler != nullptr)
				m_update_handler->Update(this, msg);
		}
	private:
		std::unique_ptr<UpdateHandler> m_update_handler;
	protected:
		const size_t max_size;
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

std::condition_variable co_cv;
std::mutex co_cv_mutex;

class ConsoleOutput : public BulkManager::Observer {
	std::queue<StringVector> m_bulks;
	bool shutdown = false;
	ThreadStat m_stat;
	std::thread m_thread;

	static void worker(std::queue<StringVector> &q, ThreadStat &stat, const bool &shutdown) {
		while (true) {
			std::unique_lock<std::mutex> lk(co_cv_mutex);
			co_cv.wait(lk, [&](){
					return !q.empty() || shutdown;
			});
			if (shutdown && q.empty())
				return;
			auto m = q.front();
			q.pop();
			lk.unlock();
			for (const auto &item : m) {
				std::cout << (&item == &m.front() ? "bulk: " : ", ") << item;
				++stat.cmd_count;
			}
			std::cout << std::endl;
			++stat.bulk_count;
		}
	}

	void JoinThreads() {
		if (shutdown)
			return;
		shutdown = true;
		co_cv.notify_all();
		m_thread.join();
	}
public:
	ConsoleOutput(const int size) : Observer(size, new SizedHandler)
		, m_stat("log")
		, m_thread(worker, std::ref(m_bulks), std::ref(m_stat), std::ref(shutdown))
	{};
	~ConsoleOutput() {
		JoinThreads();
	}

	void PostBulk(StringVector &bulk) override {
		if (bulk.empty())
			return;
		{
			std::lock_guard<std::mutex> lk(co_cv_mutex);
			m_bulks.push(bulk);
		}
		co_cv.notify_one();
	}

	void PrintStat() override {
		JoinThreads();
		std::cout << m_stat.name << " поток - "
			<< m_stat.bulk_count << " блок, "
			<< m_stat.cmd_count << " команд" << std::endl;
	}
};

std::condition_variable fo_cv;
std::mutex fo_cv_mutex;

class FileOutput : public BulkManager::Observer {
	int cmd_time;
	std::queue<StringVector> m_bulks;
	bool shutdown = false;

	ThreadStat m_stat1;
	std::thread m_thread1;
	ThreadStat m_stat2;
	std::thread m_thread2;

	static void worker(std::queue<StringVector> &q, ThreadStat &stat, const bool &shutdown) {
		while (true) {
			std::unique_lock<std::mutex> lk(fo_cv_mutex);
			fo_cv.wait(lk, [&](){
					return !q.empty() || shutdown;
			});
			if (shutdown && q.empty())
				return;
			auto m = q.front();
			q.pop();
			lk.unlock();
			std::ofstream file("bulk_"
					+ stat.name + "_"
					+ std::to_string(std::time(0))
					+ ".log");
			for (const auto &item : m) {
				file << item << std::endl;
				++stat.cmd_count;
			}
			++stat.bulk_count;
		}
	}

	void JoinThreads() {
		if (shutdown)
			return;
		shutdown = true;
		fo_cv.notify_all();
		m_thread1.join();
		m_thread2.join();
	}
public:
	FileOutput(const int size) : Observer(size, new SizedHandler)
		, m_stat1("file1")
		, m_thread1(worker, std::ref(m_bulks), std::ref(m_stat1), std::ref(shutdown))
		, m_stat2("file2")
		, m_thread2(worker, std::ref(m_bulks), std::ref(m_stat2), std::ref(shutdown))
	{};

	~FileOutput() {
		JoinThreads();
	}

	void PostBulk(StringVector &bulk) override {
		if (bulk.empty())
			return;

		{
			std::lock_guard<std::mutex> lk(fo_cv_mutex);
			m_bulks.push(bulk);
		}
		fo_cv.notify_one();
	}

	void PrintStat() override {
		JoinThreads();
		std::cout << m_stat1.name << " поток - "
			<< m_stat1.bulk_count << " блок, "
			<< m_stat1.cmd_count << " команд" << std::endl;
		std::cout << m_stat2.name << " поток - "
			<< m_stat2.bulk_count << " блок, "
			<< m_stat2.cmd_count << " команд" << std::endl;
	}
};

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
		BulkManager::ObsPtr fo(new FileOutput(bulk_size));

		MgrPtr bulk_mgr(new BulkManager());
		bulk_mgr->Subscribe(co);
		bulk_mgr->Subscribe(fo);
		bulk_mgr->Listen();

		co->PrintStat();
		fo->PrintStat();
	} catch(const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	return 0;
}
