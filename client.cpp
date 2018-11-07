/*
    Based on original assignment by: Dr. R. Bettati, PhD
    Department of Computer Science
    Texas A&M University
    Date  : 2013/01/31
 */


#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

#include <sys/time.h>
#include <cassert>
#include <assert.h>

#include <cmath>
#include <numeric>
#include <algorithm>

#include <list>
#include <vector>
#include <atomic>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <mutex>
#include <condition_variable>
#include "reqchannel.h"
#include "SafeBuffer.h"
#include "Histogram.h"
using namespace std;


struct SafeCount {
    atomic<int> value;

		void increment() {
			++value;
		}
    void decrement(){
        --value;
    }
		int set(int v) {
			this->value = v;
		}

    int get(){
        return value.load();
    }
};

struct Requester {
	SafeBuffer *safeBuff;
	string name;
	int requestCount;
	int id;
	mutex *requestLock;
	condition_variable *workerWaitCond;
	condition_variable *requestSizeCond;
	SafeCount * requestersAlive;
	int requestLimit;
	vector<Requester> *requestersVector;
	Requester(SafeBuffer * safeBuff, vector<Requester> *requestersVector, string name, int requestCount, int requestLimit, mutex *requestLock, condition_variable *requestSizeCond, SafeCount * requestersAlive, condition_variable *workerWaitCond) {
		this->safeBuff = safeBuff;
		this->requestersVector = requestersVector;
		this->name = name;
		this->requestCount = requestCount;
		this->requestLimit = requestLimit;
		this->requestLock = requestLock;
		this->requestSizeCond = requestSizeCond;
		this->workerWaitCond = workerWaitCond;
		this->requestersAlive = requestersAlive;
	}
};

struct Stat {
	SafeBuffer *buff;
	Histogram * hist;
	condition_variable *statCond;
	SafeCount * workersAlive;
	string name;
	Stat(Requester * requester, SafeCount * workersAlive, Histogram * hist) {
		this->name = requester->name;
		this->buff = new SafeBuffer();
		this->workersAlive = workersAlive;
		this->hist = hist;
		this->statCond = new condition_variable();
	}
};



struct Worker {
	SafeBuffer *safeBuff;
	int requestsCompleted;
	int id;
	Histogram * hist;
	condition_variable *requestSizeCond;
	condition_variable *workerWaitCond;
	SafeCount * requestersAlive;
	SafeCount * workersAlive;
	vector<Stat> *statsVector;
	mutex *workerLock;
	RequestChannel * workerChannel;
	Worker(SafeBuffer * safeBuff, RequestChannel *workerChannel, Histogram * hist, int id, condition_variable *requestSizeCond, SafeCount * requestersAlive, condition_variable *workerWaitCond, mutex *workerLock, SafeCount * workersAlive, vector<Stat> *statsVector ) {
		this->safeBuff = safeBuff;
		this->hist = hist;
		this->workerChannel = workerChannel;
		this->id = id;
		this->requestSizeCond = requestSizeCond;
		this->workerWaitCond = workerWaitCond;
		this->requestersAlive = requestersAlive;
		this->workerLock = workerLock;
		this->statsVector = statsVector;
		this->workersAlive = workersAlive;
	}
};


void* stat_thread_function(void* arg) {
	Stat currStat = *( (Stat*) arg);
	mutex statLock;
	cout << "Hi, I am a stat " << currStat.name << endl;
	unique_lock<mutex> condLock(statLock);
	while ( currStat.workersAlive->get() > 0 || currStat.buff->size() > 0 ) {
    if ( currStat.buff->size() > 0 ) {
      string statString = currStat.buff->pop();
      size_t pos = statString.find("@");
      string request = statString.substr(0, pos);
      statString.erase(0, pos + 1);
      string response = statString.substr(0, pos);
      // cout << " stat : " << request << " " << response << endl;
      currStat.hist->update(request, response);
      usleep(500);
    }


	}
	return 0;

}

void* request_thread_function(void* arg) {
	Requester currentRequester = *( (Requester*) arg);
	unique_lock<mutex> condLock(*currentRequester.requestLock);
	string request = "data " + currentRequester.name;
	cout << currentRequester.name << " is making their " <<  currentRequester.requestCount << " requests" << endl;

	for(int i = 0; i < currentRequester.requestCount; i++) {
		while ( currentRequester.safeBuff->size() > currentRequester.requestLimit ) {
			currentRequester.requestSizeCond->wait(condLock);
		}
		currentRequester.safeBuff->push(request);
		currentRequester.workerWaitCond->notify_one();
	}


	 currentRequester.requestersAlive->decrement();
	 cout << currentRequester.name << " done. Alive:  " << currentRequester.requestersAlive->get() << endl;
	 return 0;

}

void* worker_thread_function(void* arg) {
	Worker currentWorker = *( (Worker*) arg);
	mutex workerLock;
	int startRequests = currentWorker.safeBuff->size();
	int workUpdate = startRequests;
	cout << "Worker " << currentWorker.id << " is helping with remaining " << startRequests << " requests " << endl;
	cout << "There are " << currentWorker.requestersAlive->get() << " requesters alive " << endl;
	unique_lock<mutex> condLock(workerLock);


	while ( currentWorker.requestersAlive->get() > 0 || currentWorker.safeBuff->size() > 0 ) {
		while ( currentWorker.safeBuff->size() == 0 ) {
			currentWorker.workerWaitCond->wait(condLock);
		}

		int remainingRequests = currentWorker.safeBuff->size();
		string request =  currentWorker.safeBuff->pop();
		currentWorker.requestSizeCond->notify_one();
		currentWorker.workerChannel->cwrite(request);
		string response = currentWorker.workerChannel->cread();
		for ( int i = 0; i < currentWorker.statsVector->size(); i++ ) {
			string statName = currentWorker.statsVector->at(i).name;
			size_t found = request.find(statName);
			if ( found != string::npos ) {
				currentWorker.statsVector->at(i).buff->push(request + '@' + response);
			}
		}
		//currentWorker.hist->update(request, response);
	}



	currentWorker.workersAlive->decrement();
	currentWorker.workerWaitCond->notify_one();
	cout << "Workers Alive " << currentWorker.workersAlive->get() << " requests left : " << currentWorker.safeBuff->size() << endl << endl;
	// for ( int i = 0; i < currentWorker.statsVector->size(); i++ ) {
	// 	currentWorker.statsVector->at(i).statCond->notify_one();
	// }
	currentWorker.workerChannel->cwrite("quit");
  delete currentWorker.workerChannel;




}



/*--------------------------------------------------------------------------*/
/* MAIN FUNCTION */
/*--------------------------------------------------------------------------*/

int main(int argc, char * argv[]) {
    int requestCount = 100; //default number of requests per "patient"
    int workerCount = 1; //default number of worker threads
		int requestLimit = 10;
    int opt = 0;
    while ((opt = getopt(argc, argv, "n:w:b:")) != -1) {
        switch (opt) {
            case 'n':
                requestCount = atoi(optarg);
                break;
            case 'w':
                workerCount = atoi(optarg); //This won't do a whole lot until you fill in the worker thread function
                break;
						case 'b':
							requestLimit = atoi(optarg);
							break;
			}
    }

  int pid = fork();
	if (pid == 0){
		execl("dataserver", (char*) NULL);
	}
	else {
				SafeBuffer request_buffer;
				SafeCount requestersAlive;
				SafeCount workersAlive;
				requestersAlive.set(3);
				workersAlive.set(workerCount);
				mutex requestLock;
				mutex workerLock;
				condition_variable requestSizeCond;
				condition_variable workerWaitCond;
				Histogram hist;
				vector<Requester> requesters;
				vector<Stat> stats;
				vector<Worker> workers;
				pthread_t requestThreads[3];
				pthread_t statThreads[3];
				pthread_t workerThreads[workerCount];
				RequestChannel *chan = new RequestChannel("control", RequestChannel::CLIENT_SIDE);
        cout << "n == " << requestCount << endl;
        cout << "w == " << workerCount << endl;
				cout << "b == " << requestLimit << endl;
        cout << "CLIENT STARTED:" << endl;
        cout << "Establishing control channel... " << flush;
        cout << "done." << endl<< flush;


				requesters.push_back( Requester(&request_buffer, &requesters, "John Smith", requestCount, requestLimit, &requestLock, &requestSizeCond, &requestersAlive, &workerWaitCond) );
				requesters.push_back( Requester(&request_buffer, &requesters, "Jane Smith", requestCount, requestLimit, &requestLock, &requestSizeCond, &requestersAlive, &workerWaitCond) );
				requesters.push_back( Requester(&request_buffer, &requesters, "Joe Smith", requestCount, requestLimit, &requestLock, &requestSizeCond, &requestersAlive, &workerWaitCond) );



				pthread_create(&requestThreads[0], NULL, request_thread_function, (void*) &requesters[0]);
				pthread_create(&requestThreads[1], NULL, request_thread_function, (void*) &requesters[1]);
				pthread_create(&requestThreads[2], NULL, request_thread_function, (void*) &requesters[2]);

				stats.push_back( Stat(&requesters[0], &workersAlive, &hist) );
				stats.push_back( Stat(&requesters[1], &workersAlive, &hist) );
				stats.push_back( Stat(&requesters[2], &workersAlive, &hist) );

				// pthread_join(requestThreads[0], NULL);
				// pthread_join(requestThreads[1], NULL);
				// pthread_join(requestThreads[2], NULL);


        for(int i = 0; i < workerCount; i++) {
					chan->cwrite("newchannel");
					string newChannelName = chan->cread();
					RequestChannel * workerChannel = new RequestChannel(newChannelName, RequestChannel::CLIENT_SIDE);
          workers.push_back( Worker(&request_buffer, workerChannel, &hist, i, &requestSizeCond, &requestersAlive, &workerWaitCond, &workerLock, &workersAlive, &stats) );
					pthread_create(&workerThreads[i], NULL, worker_thread_function, (void*) &workers[i]);
					usleep(12000);
        }

				for( int i = 0; i < workerCount; i++) {
					pthread_join(workerThreads[i], NULL);
				}


				pthread_create(&statThreads[0], NULL, stat_thread_function, (void*) &stats[0]);
				pthread_create(&statThreads[1], NULL, stat_thread_function, (void*) &stats[1]);
				pthread_create(&statThreads[2], NULL, stat_thread_function, (void*) &stats[2]);


				pthread_join(statThreads[0], NULL);
				pthread_join(statThreads[1], NULL);
				pthread_join(statThreads[2], NULL);



        cout << "done." << endl;

        chan->cwrite ("quit");
        delete chan;
				hist.print();
    }
}





















//original else block
// cout << "n == " << n << endl;
// cout << "w == " << w << endl;
//
// cout << "CLIENT STARTED:" << endl;
// cout << "Establishing control channel... " << flush;
// RequestChannel *chan = new RequestChannel("control", RequestChannel::CLIENT_SIDE);
// cout << "done." << endl<< flush;
//
// SafeBuffer request_buffer;
// Histogram hist;
//
// for(int i = 0; i < n; ++i) {
// 		request_buffer.push("data John Smith");
// 		request_buffer.push("data Jane Smith");
// 		request_buffer.push("data Joe Smith");
// }
// cout << "Done populating request buffer" << endl;
//
// cout << "Pushing quit requests... ";
// for(int i = 0; i < w; ++i) {
// 		request_buffer.push("quit");
// }
// cout << "done." << endl;
//
//
// chan->cwrite("newchannel");
// string s = chan->cread (); //s = data1_ ???
// cout << "s : " << s << endl;
// RequestChannel *workerChannel = new RequestChannel(s, RequestChannel::CLIENT_SIDE);
//
// while(true) {
// 		string request = request_buffer.pop();
// 		workerChannel->cwrite(request);
//
// 		if(request == "quit") {
// 			delete workerChannel;
// 			break;
// 		} else {
// 				string response = workerChannel->cread();
// 				hist.update (request, response);
// 		}
// }
// chan->cwrite ("quit");
// delete chan;
// cout << "All Done!!!" << endl;
// hist.print ();
