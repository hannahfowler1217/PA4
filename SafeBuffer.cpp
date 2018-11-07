#include "SafeBuffer.h"
#include <string>
#include <queue>
#include <mutex>
#include <shared_mutex>
using namespace std;

SafeBuffer::SafeBuffer() {

}

SafeBuffer::~SafeBuffer() {

}

int SafeBuffer::size() {
	/*
	Is this function thread-safe???
	Make necessary modifications to make it thread-safe
	*/
	buffMtx.lock();
	int size = q.size();
	buffMtx.unlock();
  return size;
}

void SafeBuffer::push(string str) {
	/*
	Is this function thread-safe???
	Make necessary modifications to make it thread-safe
	*/
	buffMtx.lock();
	q.push (str);
	buffMtx.unlock();
}

string SafeBuffer::pop() {
	/*
	Is this function thread-safe???
	Make necessary modifications to make it thread-safe
	*/
	buffMtx.lock();
	string s = q.front();
	q.pop();
	buffMtx.unlock();
	return s;

}
