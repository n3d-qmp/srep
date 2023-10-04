#include "util.hpp"
#include <stdio.h>

Readable::~Readable(){}

// ===============
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
const size_t File::seek(const int64_t offset, const int whence)
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

// ===============
Cursor::Cursor(void* b, size_t s): buf(b),size(s),capacity(s),owned(false),pos(0){
	if (b==NULL){
		buf = malloc(s);	
		owned = true;
	}
}
Cursor::Cursor(void* b, size_t s,size_t c): buf(b),size(s),capacity(c),owned(false),pos(0){
	printf("BP: %p\n", b);
	if (b==NULL){
		buf = malloc(c);
		owned =true;
	}
}
Cursor::~Cursor(){
	if(owned&&buf)
		free(buf);
}
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
	pos+=csize;
	return csize;
}
const size_t Cursor::seek(const int64_t offset, const int whence)
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
const size_t Cursor::write(void* buf, const size_t size)
{
	if (buf==NULL)
		return 0;
	if (this->buf==NULL)
		return 0;
	size_t rsz = size;
	if (this->pos+size>this->capacity)
	{
		if (!owned)
			rsz = this->size+size-this->capacity;
		else{
			this->buf = realloc(this->buf, this->size+size);
			this->capacity=this->pos+size;
		}
	}
	if (this->pos+rsz>this->size){
		this->size = this->pos+rsz;
	}
	memcpy(this->buf+pos, buf, rsz);
	this->pos+=rsz;
	return rsz;
}

void* Cursor::get_buf(){return buf;}

// ===========
CallbackRW::CallbackRW(read_t rf, seek_t sf, write_t wf, void* opaq):rf(rf),sf(sf),wf(wf),opaque(opaq){}
void* CallbackRW::get_opaque(){return opaque;}
const size_t CallbackRW::read(void* buf, const size_t size)
{
	if(!rf)
		return 0;
	return rf(opaque,buf,size);
}
const size_t CallbackRW::seek(const int64_t offset, const int whence)
{
	return sf(opaque,offset,whence);
}
const size_t CallbackRW::write(void* buf, const size_t size)
{
	if(!wf)
		return 0;
	return wf(opaque,buf,size);
}

#ifdef __cplusplus
extern "C"{
#endif

Writable* cbio_rw_new(read_t rf, seek_t sf, write_t wf, void* opaq){return new CallbackRW(rf,sf,wf,opaq);}
void cbio_rw_drop(CallbackRW* crw){delete crw;}

Readable* w2r(Writable* w){return w;}

#ifdef __cplusplus
}
#endif