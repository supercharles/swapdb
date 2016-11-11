/*
Copyright (c) 2012-2014 The SSDB Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
*/
// todo r2m adaptation : remove this
#include "t_kv.h"
#include "codec/encode.h"
#include "codec/decode.h"

int SSDBImpl::SetGeneric(const std::string &key, const std::string &val, int flags, const int64_t expire, char log_type){
	if (expire < 0){
		return -1;
	}

	Transaction trans(binlogs);

    std::string key_type;
    if (type(key, &key_type) == -1){
        return -1;
    }

    if ((flags & OBJ_SET_NX) && (key_type != "none")){
        return -1;
    } else if ((flags & OBJ_SET_XX) && (key_type == "none")){
        return -1;
    }

	if (key_type != "none"){
		DelKeyByType(key, key_type);
	}

	if (expire > 0){
        expiration->set_ttl_internal(key, expire);
	}

	std::string meta_key = encode_meta_key(key);
	std::string meta_val = encode_kv_val(val);
	binlogs->Put(meta_key, meta_val);
	binlogs->add_log(log_type, BinlogCommand::KSET, meta_key);
	leveldb::Status s = binlogs->commit();
	if (!s.ok()){
        //todo 时间戳排序回滚fast_keys first_timeout
		return -1;
	}
    return 0;
}

int SSDBImpl::multi_set(const std::vector<Bytes> &kvs, int offset, char log_type){
	Transaction trans(binlogs);

	std::vector<Bytes>::const_iterator it;
	it = kvs.begin() + offset;
	for(; it != kvs.end(); it += 2){
		const Bytes &key = *it;
        std::string key_type;
        int ret = type(key, &key_type);
        if (ret == -1){
            return  -1;
        } else if (ret == 1){
            DelKeyByType(key, key_type);
        }

		const Bytes &val = *(it + 1);
		std::string buf = encode_meta_key(key.String());
        std::string meta_val = encode_kv_val(val.String());
		binlogs->Put(buf, meta_val);
		binlogs->add_log(log_type, BinlogCommand::KSET, buf);
	}
	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("multi_set error: %s", s.ToString().c_str());
		return -1;
	}
	return (kvs.size() - offset)/2;
}

int SSDBImpl::multi_del(const std::vector<Bytes> &keys, int offset, char log_type){ //注：redis中不支持该接口
	Transaction trans(binlogs);

	std::vector<Bytes>::const_iterator it;
	it = keys.begin() + offset;
	for(; it != keys.end(); it++){
		const Bytes &key = *it;
// todo r2m adaptation
        std::string key_type;
        int ret = type(key, &key_type);
        if (ret == -1){
            return -1;
        } else if(ret == 1){
            DelKeyByType(key, key_type);
        }
	}
	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("multi_del error: %s", s.ToString().c_str());
		return -1;
	}
	return keys.size() - offset;
}

int SSDBImpl::set(const Bytes &key, const Bytes &val, char log_type){
    return SetGeneric(key.String(), val.String(), OBJ_SET_NO_FLAGS, 0);
}

int SSDBImpl::setnx(const Bytes &key, const Bytes &val, char log_type){
    return SetGeneric(key.String(), val.String(), OBJ_SET_NX, 0);
}

int SSDBImpl::getset(const Bytes &key, std::string *val, const Bytes &newval, char log_type){
    Transaction trans(binlogs);
    int ret = get(key, val);
    if ( ret == -1){
        return -1;
    }

	std::string buf = encode_meta_key(key.String());
    std::string meta_val = encode_kv_val(newval.String());
	binlogs->Put(buf, meta_val);
	binlogs->add_log(log_type, BinlogCommand::KSET, buf);
	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("set error: %s", s.ToString().c_str());
		return -1;
	}
	return ret;
}


int SSDBImpl::del(const Bytes &key, char log_type){
    std::string key_type;
    int ret = type(key, &key_type);
    if (ret != 1){
        return ret;
    }
    if (key_type == "string"){
        ret = KDel(key);
    } else if (key_type == "hash"){
        ret = hclear(key);
    } else if (key_type == "set"){
        //todo
    } else if (key_type == "zset"){
        //todo
    } else if (key_type == "list"){
        //todo
    }

/*	Transaction trans(binlogs);

// todo r2m adaptation
	std::string buf = encode_kv_key(key);
	binlogs->Delete(buf);
	binlogs->add_log(log_type, BinlogCommand::KDEL, buf);
	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("del error: %s", s.ToString().c_str());
		return -1;
	}*/
	return 1;
}

int SSDBImpl::incr(const Bytes &key, int64_t by, int64_t *new_val, char log_type){
	Transaction trans(binlogs);

	std::string old;
	int ret = this->get(key, &old);
	if(ret == -1){
		return -1;
	}else if(ret == 0){
		*new_val = by;
	}else{
        int64_t oldvalue = str_to_int64(old);
        if(errno != 0){
            return 0;
        }
        if ((by < 0 && oldvalue < 0 && by < (LLONG_MIN-oldvalue)) ||
            (by > 0 && oldvalue > 0 && by > (LLONG_MAX-oldvalue))) {
            return 0;
        }
		*new_val = oldvalue + by;
	}

	std::string buf = encode_meta_key(key.String());
    std::string meta_val = encode_kv_val(str(*new_val));
	binlogs->Put(buf, meta_val);
	binlogs->add_log(log_type, BinlogCommand::KSET, buf);

	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("del error: %s", s.ToString().c_str());
		return -1;
	}
	return 1;
}

int SSDBImpl::get(const Bytes &key, std::string *val){
	std::string buf = encode_meta_key(key.String());
    std::string en_val;

	leveldb::Status s = ldb->Get(leveldb::ReadOptions(), buf, &en_val);
	if(s.IsNotFound()){
		return 0;
	}
	if(!s.ok()){
		log_error("get error: %s", s.ToString().c_str());
		return -1;
	}
    KvMetaVal kv;
    if (kv.DecodeMetaVal(en_val) == -1){
        return -1;
    } else{
        *val = kv.value;
    }
	return 1;
}

// todo r2m adaptation
KIterator* SSDBImpl::scan(const Bytes &start, const Bytes &end, uint64_t limit){    //不支持kv scan，redis也不支持
	std::string key_start, key_end;
	key_start = encode_kv_key(start);
	if(end.empty()){
		key_end = "";
	}else{
		key_end = encode_kv_key(end);
	}
	//dump(key_start.data(), key_start.size(), "scan.start");
	//dump(key_end.data(), key_end.size(), "scan.end");

	return new KIterator(this->iterator(key_start, key_end, limit));
}

// todo r2m adaptation
KIterator* SSDBImpl::rscan(const Bytes &start, const Bytes &end, uint64_t limit){   //不支持kv scan，redis也不支持
	std::string key_start, key_end;

	key_start = encode_kv_key(start);
	if(start.empty()){
		key_start.append(1, 255);
	}
	if(!end.empty()){
		key_end = encode_kv_key(end);
	}
	//dump(key_start.data(), key_start.size(), "scan.start");
	//dump(key_end.data(), key_end.size(), "scan.end");

	return new KIterator(this->rev_iterator(key_start, key_end, limit));
}

int SSDBImpl::setbit(const Bytes &key, int bitoffset, int on, char log_type){
	Transaction trans(binlogs);
	
	std::string val;
	int ret = this->get(key, &val);
	if(ret == -1){
		return -1;
	}

    int len = bitoffset >> 3;
    int bit = 7 - (bitoffset & 0x7);
	if(len >= val.size()){
		val.resize(len + 1, 0);
	}
	int orig = val[len] & (1 << bit);
	if(on == 1){
		val[len] |= (1 << bit);
	}else{
		val[len] &= ~(1 << bit);
	}

	std::string buf = encode_meta_key(key.String());
    std::string meta_val = encode_kv_val(val);
	binlogs->Put(buf, meta_val);
	binlogs->add_log(log_type, BinlogCommand::KSET, buf);
	leveldb::Status s = binlogs->commit();
	if(!s.ok()){
		log_error("set error: %s", s.ToString().c_str());
		return -1;
	}
	return orig;
}

int SSDBImpl::getbit(const Bytes &key, int bitoffset){
	std::string val;
	int ret = this->get(key, &val);
	if(ret == -1){
		return -1;
	}

    int len = bitoffset >> 3;
    int bit = 7 - (bitoffset & 0x7);
	if(len >= val.size()){
		return 0;
	}
	return (val[len] & (1 << bit)) == 0? 0 : 1;
}

/*
 * private API
 */
int SSDBImpl::DelKeyByType(const Bytes &key, const std::string &type){
	//todo 内部接口，保证操作的原子性，调用No Commit接口
	int ret = 0;
	if ("string" == type){
		ret = KDelNoLock(key);
	} else if ("hash" == type){
//		s = HDelKeyNoLock(key, &res);
	} else if ("list" == type){
//		s = LDelKeyNoLock(key, &res);
	} else if ("set" == type){
//		s = SDelKeyNoLock(key, &res);
	} else if ("zset" == type){
//		s = ZDelKeyNoLock(key, &res);
	}

	return 0;
}

int SSDBImpl::KDel(const Bytes &key, char log_type){
    Transaction trans(binlogs);

    std::string buf = encode_meta_key(key.String());
    binlogs->Delete(buf);
    binlogs->add_log(log_type, BinlogCommand::KDEL, buf);

    leveldb::Status s = binlogs->commit();
    if(!s.ok()){
        log_error("set error: %s", s.ToString().c_str());
        return -1;
    }
    return 1;
}

int SSDBImpl::KDelNoLock(const Bytes &key, char log_type){
	std::string buf = encode_meta_key(key.String());
	binlogs->Delete(buf);
	binlogs->add_log(log_type, BinlogCommand::KDEL, buf);
	return 0;
}

/*
 * General API
 */
int SSDBImpl::type(const Bytes &key, std::string *type){
	*type = "none";
    int ret = 0;
	std::string val;
	std::string meta_key = encode_meta_key(key.String());
	leveldb::Status s = ldb->Get(leveldb::ReadOptions(), meta_key, &val);
	if(s.IsNotFound()){
		return 0;
	}
	if(!s.ok()){
		log_error("get error: %s", s.ToString().c_str());
		return -1;
	}

	if (val[0] == DataType::KV){
		*type = "string";
        ret = 1;
	} else if (val[0] == DataType::HSIZE){
		HashMetaVal hv;
		if (hv.DecodeMetaVal(val) == -1){
			return -1;
		}
		if (hv.del == KEY_ENABLED_MASK){
			*type = "hash";
            ret = 1;
		}
	} else if (val[0] == DataType::SSIZE){
		SetMetaVal sv;
		if (sv.DecodeMetaVal(val) == -1){
			return -1;
		}
		if (sv.del == KEY_ENABLED_MASK){
			*type = "set";
            ret = 1;
		}
	} else if (val[0] == DataType::ZSIZE){
		ZSetMetaVal zs;
		if (zs.DecodeMetaVal(val) == -1){
			return -1;
		}
		if (zs.del == KEY_ENABLED_MASK){
			*type = "zset";
            ret = 1;
		}
	} else if (val[0] == DataType::LSZIE){
		ListMetaVal ls;
		if (ls.DecodeMetaVal(val) == -1){
			return -1;
		}
		if (ls.del == KEY_ENABLED_MASK){
			*type = "list";
            ret = 1;
		}
	} else{
		return -1;
	}

	return ret;
}