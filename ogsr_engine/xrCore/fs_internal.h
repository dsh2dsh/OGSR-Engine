#pragma once

#include "lzhuf.h"
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <share.h>

void*			FileDownload	(LPCSTR fn, u32* pdwSize=NULL);

class CFileWriter : public IWriter
{
private:
	FILE*			hf;

public:

  CFileWriter( const char *name, bool exclusive ) {
    R_ASSERT( name && name[ 0 ] );
    fName = name;
    VerifyPath( *fName );
    if ( exclusive ) {
      int handle;
      errno_t err = _sopen_s( &handle, *fName, _O_WRONLY|_O_TRUNC|_O_CREAT|_O_BINARY, SH_DENYWR, _S_IREAD|_S_IWRITE );
      ASSERT_FMT( err == 0, "Can't open %s, because %s", *fName, strerror( err ) );
      hf = _fdopen( handle, "wb" );
      ASSERT_FMT( hf, "Can't open %s, because %s", *fName, strerror( errno ) );
    }
    else {
      hf = fopen( *fName, "wb" );
      ASSERT_FMT( hf, "Can't open %s, because %s", *fName, strerror( errno ) );
    }
  }

	virtual 		~CFileWriter()
	{
		if (0!=hf){	
        	fclose				(hf);
        	// release RO attrib
	        DWORD dwAttr 		= GetFileAttributes(*fName);
	        if ((dwAttr != u32(-1))&&(dwAttr&FILE_ATTRIBUTE_READONLY)){
                dwAttr 			&=~ FILE_ATTRIBUTE_READONLY;
                SetFileAttributes(*fName, dwAttr);
            }
        }
	}
	// kernel
	virtual void	w			(const void* _ptr, u32 count) 
    { 
		if ((0!=hf) && (0!=count)){
			u8* ptr = (u8*)_ptr;
			size_t W = fwrite( ptr, 1, count, hf );
			R_ASSERT3( W == count, "Can't write mem block to file. Disk maybe full.", _sys_errlist[ errno ] );
		}
    };
	virtual void	seek		(u32 pos)	{	if (0!=hf) fseek(hf,pos,SEEK_SET);		};
	virtual u32		tell		()			{	return (0!=hf)?ftell(hf):0;				};
	virtual bool	valid		()			{	return (0!=hf);}
	virtual int		flush		()			{	return fflush(hf);}
};

// It automatically frees memory after destruction
class CTempReader : public IReader
{
public:
				CTempReader(void *_data, int _size, int _iterpos) : IReader(_data,_size,_iterpos)	{}
	virtual		~CTempReader();
};
class CPackReader : public IReader
{
	void*		base_address;
public:
				CPackReader(void* _base, void* _data, int _size) : IReader(_data,_size){base_address=_base;}
	virtual		~CPackReader();
};
class CFileReader : public IReader
{
public:
				CFileReader(const char *name);
	virtual		~CFileReader();
};
class CVirtualFileReader : public IReader
{
private:
   void			*hSrcFile, *hSrcMap;
public:
				CVirtualFileReader(const char *cFileName);
	virtual		~CVirtualFileReader();
};
