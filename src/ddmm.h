#ifndef NELEM
#define NELEM(x) ((sizeof(x)/sizeof(x[0])))
#endif

BOOL __cdecl _tcscatPath(TCHAR *szDest,size_t cchDestSize,const TCHAR * restrict szPath1,...);
