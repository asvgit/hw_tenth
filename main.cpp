#include <iostream>
#include <cassert>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <fstream>
#include <ctime>
#include <memory>

using string = std::string;
using StringVector = std::vector<string>;

class BulkManager {
public:
	class Observer : public std::enable_shared_from_this<Observer> {
	public:
		class UpdateHandler {
		public:
			virtual ~UpdateHandler() = default;
			virtual void Update(Observer *, const string &) = 0;
		};


		Observer(const int size, UpdateHandler *uh) : m_update_handler(uh), max_size(size) {}
		virtual ~Observer() = default;
		std::shared_ptr<Observer> GetPtr() { return shared_from_this(); }
		void SetUpdadeHandler(UpdateHandler *uh) { m_update_handler.reset(uh); };
		StringVector& GetBulk() { return m_bulk; }
		int GetMaxSize() { return max_size; }
		virtual void PostBulk() = 0;
		void Update(const string &msg) {
			if (m_update_handler != nullptr)
				m_update_handler->Update(this, msg);
		}
	private:
		std::unique_ptr<UpdateHandler> m_update_handler;
	protected:
		StringVector m_bulk;
		const int max_size;
	};
	using ObsPtr = std::shared_ptr<Observer>;

	void Subscribe(ObsPtr &obs) { m_subs.push_back(obs->GetPtr()); }

	void Listen() {
		for (string line; std::getline(std::cin, line);)
			Notify(line);
		Post();
	}

private:
	std::vector<ObsPtr> m_subs;

	void Notify(const string &chunk) {
		for (const auto &s : m_subs)
			s->Update(chunk);
	};

	void Post() {
		for (const auto &s : m_subs)
			s->PostBulk();
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
		auto &bulk = o->GetBulk();
		if (cmd == "{") {
			o->PostBulk();
			bulk.clear();
			o->SetUpdadeHandler(new DynamicHandler());
			return;
		}

		bulk.push_back(cmd);
		if (bulk.size() >= o->GetMaxSize()) {
			o->PostBulk();
			bulk.clear();
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
		auto &bulk = o->GetBulk();
		o->GetBulk() = m_bulk;
		o->PostBulk();
		bulk.clear();
		m_bulk.clear();
		o->SetUpdadeHandler(new SizedHandler());
	} else
		m_bulk.push_back(cmd);
}

class ConsoleOutput : public BulkManager::Observer {
public:
	ConsoleOutput(const int size) : Observer(size, new SizedHandler) {};

	void PostBulk() override {
		if (m_bulk.empty())
			return;
#ifdef NDEBUG
		sleep(1);
#endif
		for (auto &n : m_bulk) {
			std::cout << (&n == &m_bulk.front() ? "bulk: " : ", ")
					<< n;
		}
		std::cout << std::endl;
	}
};

class FileOutput : public BulkManager::Observer {
	int cmd_time;

public:
	FileOutput(const int size) : Observer(size, new SizedHandler) {};

	void PostBulk() override {
		if (m_bulk.empty())
			return;

		std::ofstream file("bulk" + std::to_string(std::time(0)) + ".log");
		for (auto &n : m_bulk)
			file << n << std::endl;
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
	} catch(const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}
	return 0;
}
