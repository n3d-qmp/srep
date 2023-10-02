#include "util.hpp"
#include <stdio.h>

Readable::~Readable(){}

File::File(FILE* f):file(f),close_(false){}
File::File(File& f):file(f.file),close_(false){}
File::File(const char* p, const char* m):close_(true)
{
	file = fopen(p,m);
}
File::~File(){
	if (file==NULL) return;
	if (close_)
		fclose(file);
}
const size_t File::read(void* buf, const size_t size)
{
	if (file==NULL)
		return 0;
	return fread(buf,1,size,file);
}
const size_t File::write(void* buf, const size_t size)
{
	if (file==NULL)
		return 0;
	return fwrite(buf,1,size,file);
}
const int File::seek(const int64_t offset, const int whence)
{
	if (file==NULL)
		return -1;
	return fseek(file,offset,whence);
}
void File::close(){
	if (file==NULL) return;
	fclose(file);
}
FILE* File::get(){
	return file;
}

Cursor::Cursor(void* b, size_t s): buf(b),size(s),pos(0){}
Cursor::~Cursor(){}
const size_t Cursor::read(void* buf, const size_t size)
{
	if (buf==NULL)
		return 0;
	if (this->buf==NULL)
		return 0;
	if (pos==this->size)
		return 0;
	const size_t csize = std::min(size, this->size-pos);
	memcpy(buf, this->buf+pos, csize);
	printf("R: %d %d %d %d\n", csize, size, this->size, pos);
	pos+=csize;
	return csize;
}
const int Cursor::seek(const int64_t offset, const int whence)
{
	if (buf==NULL)
		return -1;
	switch(whence)
	{
case SEEK_CUR:
	{
		pos=std::min(pos+offset,size);
		return pos;
	}
case SEEK_SET:
	{
		pos = std::min((size_t)std::max(offset, (int64_t)0), size);
		return pos;
	}
case SEEK_END:
	{
		pos=std::min(size+offset,size);
		return pos;
	}
default:
	return pos;
}
}