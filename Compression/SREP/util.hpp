#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

class Readable{
public:
	virtual const size_t read(void* buf, const size_t size)=0;
	virtual const int seek(const int64_t, const int)=0;
	virtual ~Readable();
};
class Writable:public Readable{
public:
	virtual const size_t write(void* buf, const size_t size)=0;
};
class Cursor:public Readable{
public:
	Cursor(void*, size_t);
	~Cursor();

	const size_t read(void* buf, const size_t size) override;
	const int seek(const int64_t, const int) override;
protected:
	void* buf;
	size_t size;
	size_t pos=0;
};
class File:public Writable{
public:
	File(FILE*);
	File(const char*, const char*);
	File(File&);
	~File();

	const size_t read(void* buf, const size_t size) override;
	const int seek(const int64_t, const int) override;

	const size_t write(void* buf, const size_t size) override;

	void close();
	FILE* get();
protected:
	FILE* file;
	bool close_;
};
