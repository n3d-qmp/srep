#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

class Readable{
public:
	virtual const size_t read(void* buf, const size_t size)=0;
	virtual const size_t seek(const int64_t, const int)=0;
	virtual ~Readable();
};
class Writable:public Readable{
public:
	virtual const size_t write(void* buf, const size_t size)=0;
};

class Cursor:public Writable{
public:
	Cursor(void*, size_t);
	Cursor(void*, size_t,size_t);
	~Cursor();

	const size_t read(void* buf, const size_t size) override;
	const size_t seek(const int64_t, const int) override;

	const size_t write(void* buf, const size_t size) override;

	void* get_buf();
protected:
	void* buf;
	size_t size;
	size_t capacity;
	bool owned;
	size_t pos=0;
};

class File:public Writable{
public:
	File(FILE*);
	File(const char*, const char*);
	File(File&);
	~File();

	const size_t read(void* buf, const size_t size) override;
	const size_t seek(const int64_t, const int) override;

	const size_t write(void* buf, const size_t size) override;

	void close();
	FILE* get();
protected:
	FILE* file;
	bool close_;
};

typedef const size_t (*read_t)(void*,void* buf, const size_t size);
typedef const int (*seek_t)(void*,const int64_t, const int);
typedef const size_t (*write_t)(void*,void* buf, const size_t size);

class CallbackRW:public Writable{
public:
	CallbackRW(read_t rf, seek_t sf, write_t wf, void* opaq);

	const size_t read(void* buf, const size_t size) override;
	const size_t seek(const int64_t, const int) override;

	const size_t write(void* buf, const size_t size) override;

	void* get_opaque();
protected:
	void* opaque;
	read_t rf;
	seek_t sf;
	write_t wf;
};
