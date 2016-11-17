/*
Copyright (c) 2012-2014 The SSDB Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
*/
#ifndef SSDB_ITERATOR_H_
#define SSDB_ITERATOR_H_

#include <inttypes.h>
#include <string>
#include "../util/bytes.h"

#ifdef USE_LEVELDB
namespace leveldb{
#else
#define leveldb rocksdb
namespace rocksdb{
#endif
	class Iterator;
}

class Iterator{
public:
	enum Direction{
		FORWARD, BACKWARD
	};
	Iterator(leveldb::Iterator *it,
			const std::string &end,
			uint64_t limit,
			Direction direction=Iterator::FORWARD);
	~Iterator();
	bool skip(uint64_t offset);
	bool next();
	Bytes key();
	Bytes val();
private:
	leveldb::Iterator *it;
	std::string end;
	uint64_t limit;
	bool is_first;
	int direction;
};


// todo r2m adaptation
class KIterator{
public:
	std::string key;
	std::string val;

	KIterator(Iterator *it);
	~KIterator();
	void return_val(bool onoff);
	bool next();
private:
	Iterator *it;
	bool return_val_;
};


// todo r2m adaptation
class HIterator{
public:
	std::string name;
	std::string key;
	std::string val;
	uint16_t 	version;

	HIterator(Iterator *it, const Bytes &name, uint16_t version = 0);
	~HIterator();
	void return_val(bool onoff);
	bool next();
private:
	Iterator *it;
	bool return_val_;
};


// todo r2m adaptation
class ZIterator{
public:
	std::string name;
	std::string key;
	double      score;

	uint16_t 	version;

	ZIterator(Iterator *it, const Bytes &name, uint16_t version);
	~ZIterator();
	bool skip(uint64_t offset);
	bool next();
private:
	Iterator *it;
};


#endif
