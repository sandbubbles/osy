#ifndef __PROGTEST__
#include <iostream>
#include <chrono>
#include <thread>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <semaphore.h>

#include "common.h"

using namespace std;
#endif /* __PROGTEST__ */
//=================================================================================================
class CUniqueLock
{
  public:
                             CUniqueLock                   ( pthread_mutex_t & mtx )
                             : m_Mtx ( &mtx )
    {
      Lock ();
    }
                            ~CUniqueLock                   ( void )
    {
      Unlock ();
    }
    void                     Lock                          ( void )
    {
      pthread_mutex_lock ( m_Mtx );
    }
    void                     Unlock                        ( void )
    {
      pthread_mutex_unlock ( m_Mtx );
    }
  private:
    pthread_mutex_t        * m_Mtx;
};
//=================================================================================================
class CPageMgr
{
  public:
                             CPageMgr                      ( uint32_t        * array,
                                                             uint32_t          size )
      : m_Stack ( array ),
        m_MaxSize ( size )
    {
      uint32_t i, n = sizeInPages ( size, sizeof (uint32_t) );
      m_CurrentSize = size - n;

      for ( i = 0; n < size && i < size; ++i, ++n )
        m_Stack[i] = n;

      m_Top = &( m_Stack[i] ); 

      pthread_mutex_init ( &m_Mtx, NULL );
      pthread_cond_init  ( &m_CV, NULL ); 
    }
    //---------------------------------------------------------------------------------------------
                            ~CPageMgr                      ( void )
    {
      pthread_cond_destroy ( &m_CV );
      pthread_mutex_destroy ( &m_Mtx );
    }
    //---------------------------------------------------------------------------------------------
    void                     push                          ( uint32_t          item )
    {
      CUniqueLock lock ( m_Mtx );

      while ( m_CurrentSize  >= m_MaxSize )
        pthread_cond_wait( &m_CV, &m_Mtx);
  
      * m_Top = item;
      ++ m_Top;
      ++ m_CurrentSize;

      pthread_cond_broadcast ( &m_CV );
      return;
    }
    //---------------------------------------------------------------------------------------------
    uint32_t                 get                           ( void )
    {
      CUniqueLock lock ( m_Mtx );

      while ( m_CurrentSize == 0 )
        pthread_cond_wait( &m_CV, &m_Mtx);
      
      -- m_Top;
      -- m_CurrentSize;

      uint32_t rtn = * m_Top;
      * m_Top = -1;

      pthread_cond_broadcast ( &m_CV );
      return rtn;
    }
    //---------------------------------------------------------------------------------------------
    uint32_t                 size                          ( void ) const
    {
      CUniqueLock lock ( m_Mtx );
      return m_CurrentSize;
    }
    //---------------------------------------------------------------------------------------------
    void                     print                         ( void )
    {
      cout << "[";
      for ( uint32_t i = 0; i < m_CurrentSize ; i ++ )
        cout << " " << m_Stack[i] << " ";
      cout << "]" << endl;
    }
    //---------------------------------------------------------------------------------------------

  private:
    uint32_t                 sizeInPages                   ( uint32_t          number,
                                                             uint32_t          size )
    {
      return (( number * size ) / CCPU::PAGE_SIZE ) + 1; // if the one extra page is a problem, use ternary
    }
    mutable pthread_cond_t   m_CV;
    mutable pthread_mutex_t  m_Mtx;
    uint32_t               * m_Stack;
    uint32_t               * m_Top;
    uint32_t                 m_MaxSize;
    uint32_t                 m_CurrentSize;
};
//=================================================================================================
//-------------------------------------------------------------------------------------------------

class CBB : public CCPU
{
  public:
    struct TThrArg
    {
      void                 * m_Args;
      void                (* m_EntryPoint) ( CCPU *, void * );
      pthread_t              m_ThrHandle;
      CBB                  * m_CPU;
    };
    //---------------------------------------------------------------------------------------------
                             CBB                           ( uint8_t         * memStart,
                                                             CPageMgr        * stack  )
      : CCPU ( memStart , stack -> get () * PAGE_SIZE ),
        m_PageMgr ( stack ),
        m_RootNumber ( m_PageTableRoot / PAGE_SIZE ),
        m_MemLimit ( 0 )
    {
      initPage ( m_RootNumber );
    }
    //---------------------------------------------------------------------------------------------
    virtual                 ~CBB                           ( void )
    {
      SetMemLimit ( 0 );
      m_PageMgr -> push ( m_RootNumber );
    }
    //---------------------------------------------------------------------------------------------
    virtual uint32_t         GetMemLimit                   ( void ) const
    {
      return m_MemLimit;
    }
    //---------------------------------------------------------------------------------------------
    virtual bool             SetMemLimit                   ( uint32_t          pages )
    {
      int num = pages - GetMemLimit ();

      if ( num == 0 ) 
        return true;

      if ( num > 0 ) // add pages
      {
        if ( (uint32_t) num > m_PageMgr -> size () 
             || PAGE_DIR_ENTRIES * PAGE_DIR_ENTRIES < pages  ) // can't allocate more than that
          return false;

        return allocatePages ( num );
      }

      if ( num < 0 ) // delete pages
        return deallocatePages ( - num );

      return false;
    }

    //---------------------------------------------------------------------------------------------
    virtual bool             NewProcess                    ( void            * processArg,
                                                             void           (* entryPoint) ( CCPU *, void * ),
                                                             bool              copyMem )
    {
      pthread_mutex_lock( & c_Mtx );
      while ( c_Processes >= ( PROCESS_MAX - 1 ) )
        pthread_cond_wait( &c_CV, &c_Mtx );
      
      ++ c_Processes; 
      pthread_mutex_unlock( & c_Mtx );
      pthread_cond_broadcast ( &c_CV );

      if ( copyMem )
      {
        if ( m_MemLimit + 2 + ( m_MemLimit / PAGE_DIR_ENTRIES ) > m_PageMgr -> size () )
          return false;
      }
      else
        if ( m_PageMgr -> size () < 1 )
          return false;

      TThrArg * arg = new TThrArg;
      arg -> m_Args = processArg;
      arg -> m_EntryPoint = entryPoint;

      arg -> m_CPU = new CBB ( m_MemStart, m_PageMgr );

      if ( copyMem )
        arg -> m_CPU -> copy ( this );


      pthread_attr_t     attr;
      pthread_attr_init ( &attr );                                                 
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);  

      pthread_create  ( &(arg -> m_ThrHandle) , &attr, &(CBB::thrWrapper), (void*)arg );

      return true;
    }
    //---------------------------------------------------------------------------------------------
    static void            * thrWrapper                    ( void            * arg )
    {
    
      TThrArg * a = (TThrArg*) arg;;
      
      a -> m_EntryPoint ( a -> m_CPU, a -> m_Args );

      delete a -> m_CPU;
      delete a;

      pthread_mutex_lock( & c_Mtx );
      -- c_Processes; 
      pthread_mutex_unlock( & c_Mtx );
      pthread_cond_broadcast ( &c_CV );

      return NULL;
    }
    //---------------------------------------------------------------------------------------------
    bool                     copy                          ( CCPU            * toCopy )
    {
      if ( toCopy -> GetMemLimit () > m_PageMgr -> size () )
        return false;
      
      SetMemLimit (  toCopy -> GetMemLimit () );

      for ( uint32_t i = 0 ; i < toCopy -> GetMemLimit (); i++ )
        copyPage ( getNthPage ( i ), ((CBB *)toCopy) -> getNthPage ( i ) );

      return true;
    }
    //---------------------------------------------------------------------------------------------
    void                     copyPage                      ( uint32_t          destPage,
                                                             uint32_t          srcPage )
    {
      memcpy ( ( void * ) getPointer ( destPage, 0 ) , ( void * ) getPointer ( srcPage, 0 ), PAGE_SIZE );
    }
    //---------------------------------------------------------------------------------------------
    /* From zero */
    uint32_t                 getNthPage                    ( uint32_t          whichPage )
    {
      return pgNumFromLine ( pgNumFromLine ( m_RootNumber, whichPage/PAGE_DIR_ENTRIES ),
                             whichPage % PAGE_DIR_ENTRIES );
    }
    //---------------------------------------------------------------------------------------------
    void                     Print                         ( void ) const
    {
      cout << "m_RootNumber" << m_RootNumber << endl;
      cout << "m_MemStart" << (void *) m_MemStart << endl;
      cout << "m_PageTableRoot" << m_PageTableRoot << endl;
    }
    //---------------------------------------------------------------------------------------------

  public:

    uint32_t               * getPointer                    ( uint32_t          pageNumber,
                                                             uint16_t          line )
    {
      return ( uint32_t *) ( m_MemStart + pageNumber * PAGE_SIZE + line * 4 );
    }
    //---------------------------------------------------------------------------------------------
    void                     setToZero                     ( uint32_t          pageNumber,
                                                             uint16_t          line )
    {
      uint32_t *item = getPointer ( pageNumber, line );
      *item = 0;
    }
    //---------------------------------------------------------------------------------------------
    void                     setToActive                   ( uint32_t          currentPage,
                                                             uint16_t          line,
                                                             uint32_t          newPage,
                                                             bool              canWrite = true )
    {
      uint32_t *item = getPointer ( currentPage, line );
      * item = newPage << OFFSET_BITS | BIT_PRESENT | BIT_USER;
      if ( canWrite )
        * item |=  BIT_WRITE; 
    }
    //---------------------------------------------------------------------------------------------
    bool                     isLineActive                  ( uint32_t          pageNumber,
                                                             uint16_t          line )
    {
      return ( * getPointer ( pageNumber, line )) & BIT_PRESENT;
    }
    //---------------------------------------------------------------------------------------------
    uint16_t                 numOfActiveLines              ( uint32_t          page )
    {
      uint16_t number = 0;
      for ( uint32_t i = 0 ; i < PAGE_DIR_ENTRIES; i++ )
        if ( isLineActive ( page, i ) )
          ++number;
        else
          break; 
      return number;
    }
    //---------------------------------------------------------------------------------------------
    uint32_t                 pgNumFromLine                 ( uint32_t          pageNumber,
                                                             uint16_t          line )
    {
      return (( * getPointer ( pageNumber, line ) ) & ADDR_MASK ) >> OFFSET_BITS;
    }
    //---------------------------------------------------------------------------------------------
    uint32_t                 initPage                      ( uint32_t          pageNumber )
    {
      for ( size_t i = 0; i < CCPU::PAGE_DIR_ENTRIES ; i++)
       setToZero ( pageNumber, i );

      return pageNumber;
    }
    //---------------------------------------------------------------------------------------------
    bool                     allocatePages                 ( uint32_t          toAdd )
    {
      while ( toAdd > 0 )
      {
        int line = firstNotFullLine ();
        if ( line == -1 ) return false;
        uint32_t page = pgNumFromLine ( m_RootNumber, line );

        for ( size_t i = 0; (i < PAGE_DIR_ENTRIES) && (toAdd !=0); ++i )
          if ( !isLineActive ( page, i ) )
          {
            setToActive ( page, i, m_PageMgr -> get() );
            --toAdd;
            ++ m_MemLimit;
          }
      }

      return toAdd == 0;
    }
    //---------------------------------------------------------------------------------------------
    int                      firstNotFullLine              ( void ) 
    {
    
      for ( uint16_t i = 0; i < PAGE_DIR_ENTRIES; i++ )
      {
        if ( ! isLineActive ( m_RootNumber, i ) )
        {
          uint32_t page = m_PageMgr -> get();
          setToActive ( m_RootNumber, i, page );
          initPage ( page );
          return i;
        }

        if ( numOfActiveLines ( pgNumFromLine ( m_RootNumber, i ) ) < PAGE_DIR_ENTRIES )
          return i;
      }
      return -1;
    }
    //---------------------------------------------------------------------------------------------
    bool                     deallocatePages               ( uint32_t          toSub )
    {
      while ( toSub > 0 )
      {
        int line = lastActiveLine ();
        if ( line == -1 ) return false;

        uint32_t page = pgNumFromLine ( m_RootNumber, line );

        for ( int i = ( PAGE_DIR_ENTRIES - 1 ); ( i >= 0 ) && ( toSub != 0 ); --i )
          if ( isLineActive ( page, i ) )
          {
            uint32_t empty = pgNumFromLine ( page, i );
            setToZero ( page, i );
            m_PageMgr -> push ( empty );
            --toSub;
            -- m_MemLimit;
          }

        if ( numOfActiveLines ( page ) == 0 )
        {
          setToZero ( m_RootNumber, line );
          m_PageMgr -> push ( page );
        }
      }
  
      return toSub == 0;
    }
    //---------------------------------------------------------------------------------------------
    int                      lastActiveLine                ( void )
    {
      if ( ! isLineActive ( m_RootNumber, 0 ) )
        return -1;

      uint16_t i;
      for ( i = 0; i < PAGE_DIR_ENTRIES - 1; i++ )
        if ( isLineActive ( m_RootNumber, i ) && ! isLineActive ( m_RootNumber, i + 1 ) )
          return i;
      
      if ( isLineActive ( m_RootNumber, i ) )
        return i;
      
      return -1;
    }
    //---------------------------------------------------------------------------------------------
    void                     wait                          ( void )
    {
      pthread_mutex_lock ( &c_Mtx );

      while ( c_Processes != 0 )
        pthread_cond_wait ( &c_CV, &c_Mtx );

      pthread_mutex_unlock (&c_Mtx); 
    }
                         
    CPageMgr               * m_PageMgr;
    uint32_t                 m_RootNumber;
    uint32_t                 m_MemLimit;
    static pthread_mutex_t   c_Mtx;
    static pthread_cond_t    c_CV;
    static uint32_t          c_Processes;

};
pthread_mutex_t   CBB::c_Mtx;
pthread_cond_t    CBB::c_CV;
uint32_t          CBB::c_Processes = 0;
//-------------------------------------------------------------------------------------------------

//=================================================================================================
void               MemMgr                                  ( void            * mem,
                                                             uint32_t          totalPages,
                                                             void            * processArg,
                                                             void           (* mainProcess) ( CCPU *, void * ))
{
   pthread_mutex_init ( &CBB::c_Mtx, NULL );
   pthread_cond_init ( &CBB::c_CV, NULL );

   CPageMgr stack ( (uint32_t *)mem, totalPages );
   CBB      mnu ( (uint8_t * ) mem, &stack );

   mainProcess ( &mnu, processArg );

   mnu . wait ();
      
   pthread_mutex_destroy ( &CBB::c_Mtx );
   pthread_cond_destroy ( &CBB::c_CV );
}
//=================================================================================================