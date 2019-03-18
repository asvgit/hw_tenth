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
#include <atomic>

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


		Observer(const int size, UpdateHandler *uh)
			: m_update_handler(uh), max_size(size) {}
		virtual ~Observer() = default;
		std::shared_ptr<Observer> GetPtr() { return shared_from_this(); }
		void SetUpdadeHandler(UpdateHandler *uh) { m_update_handler.reset(uh); };
		size_t GetMaxSize() { return max_size; }
		virtual void PostBulk(StringVector&) = 0;
		virtual void PrintStat() {};
		virtual void JoinThreads() {};
		virtual void Update(const string &msg) {
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
		Notify("{");
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

class StatOutput : public BulkManager::Observer {
	ThreadStat m_stat;
	size_t m_line_count;
public:
	StatOutput(const int size)
		: Observer(size, new SizedHandler), m_stat("main") {};
	void Update(const string &msg) override {
		BulkManager::Observer::Update(msg);
		++m_line_count;
	}
	void PostBulk(StringVector &bulk) override {
		if (bulk.empty())
			return;
		++m_stat.bulk_count;
		m_stat.cmd_count += bulk.size();
	}
	void PrintStat() override {
		std::cout << m_stat.name << " поток - "
			<< m_line_count << " строк, "
			<< m_stat.cmd_count << " команд, "
			<< m_stat.bulk_count << " блок" << std::endl;
	}
};

std::condition_variable co_cv;
std::mutex co_cv_mutex;

class ConsoleOutput : public BulkManager::Observer {
	std::queue<StringVector> m_bulks;
	std::atomic_bool shutdown;
	ThreadStat m_stat;
	std::thread m_thread;

	static void worker(
			  std::queue<StringVector> &q
			, ThreadStat &stat
			, const std::atomic_bool &shutdown
	) {
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

public:
	void JoinThreads() override {
		if (shutdown)
			return;
		shutdown.store(true);
		co_cv.notify_all();
		m_thread.join();
	}

	ConsoleOutput(const int size) : Observer(size, new SizedHandler)
		, m_stat("log")
		, m_thread(
				  worker
				, std::ref(m_bulks)
				, std::ref(m_stat)
				, std::ref(shutdown)
		)
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

template<int N>
class FileOutput : public BulkManager::Observer {
	int cmd_time;
	std::queue<StringVector> m_bulks;
	std::atomic_bool shutdown;

	std::vector<ThreadStat> m_stat;
	std::vector<std::thread> m_thread;

	static void worker(
			  std::queue<StringVector> &q
			, ThreadStat &stat
			, const std::atomic_bool &shutdown
	) {
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
			const string file_name = "bulk_"
					+ stat.name + "_"
					+ std::to_string(std::time(0));
			auto exists = [] (const string fname) -> bool {
				std::ifstream infile(fname + ".log");
				return infile.good();
			};
			const string unique_fname = [&] () -> string {
				string fname = file_name;
				int name_try(0);
				while (exists(fname))
					fname = file_name + "_" + std::to_string(++name_try);
				return fname + ".log";
			} ();
			std::ofstream file(unique_fname);
			for (const auto &item : m) {
				file << item << std::endl;
				++stat.cmd_count;
			}
			++stat.bulk_count;
		}
	}

public:
	void JoinThreads() override {
		if (shutdown)
			return;
		shutdown.store(true);
		fo_cv.notify_all();
		for (auto &t : m_thread)
			t.join();
	}

	FileOutput(const int size) : Observer(size, new SizedHandler) {
		m_stat.reserve(N);
		for (int i = 0; i < N; ++i) {
			m_stat.emplace(m_stat.begin() + i, "file" + std::to_string(i));
			m_thread.emplace_back(
					  worker
					, std::ref(m_bulks)
					, std::ref(m_stat.at(i))
					, std::ref(shutdown)
				);
		}

	};

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
		for (auto &s : m_stat)
			std::cout << s.name << " поток - "
				<< s.bulk_count << " блок, "
				<< s.cmd_count << " команд" << std::endl;
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

		BulkManager::ObsPtr so(new StatOutput(bulk_size));
		BulkManager::ObsPtr co(new ConsoleOutput(bulk_size));
		BulkManager::ObsPtr fo(new FileOutput<2>(bulk_size));

		MgrPtr bulk_mgr(new BulkManager());
		bulk_mgr->Subscribe(so);
		bulk_mgr->Subscribe(co);
		bulk_mgr->Subscribe(fo);
		bulk_mgr->Listen();

		co->JoinThreads();
		fo->JoinThreads();

		so->PrintStat();
		co->PrintStat();
		fo->PrintStat();
	} catch(const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	return 0;
}
