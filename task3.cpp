#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <functional>
using namespace std;

/*
  Figure out the logic in writing and reading files and write it
  Test
  Do the rest
*/

/* Filesystem size: min 8MiB, max 1GiB
 * Filename length: min 1B, max 28B
 * Sector size: 512B
 * Max open files: 8 at a time
 * At most one filesystem mounted at a time.
 * Max file size: < 1GiB
 * Max files in the filesystem: 128
 */

#define FILENAME_LEN_MAX     28
#define DIR_ENTRIES_MAX      128
#define OPEN_FILES_MAX       8
#define SECTOR_SIZE          512
#define DEVICE_SIZE_MAX      ( 1024 * 1024 * 1024 )
#define DEVICE_SIZE_MIN      ( 8 * 1024 * 1024 )

struct TFile
{
  char                       m_FileName[FILENAME_LEN_MAX + 1];
  size_t                     m_FileSize;
};

struct TBlkDev
{
  size_t                     m_Sectors;
  function<size_t(size_t, void *, size_t )> m_Read;
  function<size_t(size_t, const void *, size_t )> m_Write;
};
#endif /* __PROGTEST__ */
//-------------------------------------------------------------------------------------------------
size_t                       min                           ( size_t            first,
                                                             size_t            second )
{
  if ( first < second )
    return first;
  return second;
}
//=================================================================================================
struct TFileEnt
{
  char                       m_FileName[FILENAME_LEN_MAX + 1];
  uint                       m_FirstSector;
  uint                       m_FileSize;
};
/*
 I'll use this as an object in an array. the index in that array is the FileDescriptor.
*/
struct TFileOpen
{
  bool                       m_Mode;
  uint32_t                   m_Position; 
  uint32_t                   m_IdxFiles;
};
struct THeader
{
  uint32_t                   m_DataStartSector;
  uint32_t                   m_FirstFreeSector;
  uint32_t                   m_LastFreeSector;
  uint8_t                    m_PlaceHolder[SECTOR_SIZE - ( sizeof(uint32_t) * 3)];
};
//=================================================================================================
class CFileSystem
{
  public:
    static bool              CreateFs                      ( const TBlkDev   & dev );
    static CFileSystem     * Mount                         ( const TBlkDev   & dev );
    bool                     Umount                        ( void );
    size_t                   FileSize                      ( const char      * fileName );
    int                      OpenFile                      ( const char      * fileName,
                                                             bool              writeMode );
    bool                     CloseFile                     ( int               fd );
    size_t                   ReadFile                      ( int               fd,
                                                             void            * data,
                                                             size_t            len );
    size_t                   WriteFile                     ( int               fd,
                                                             const void      * data,
                                                             size_t            len );
    bool                     DeleteFile                    ( const char      * fileName );
    bool                     FindFirst                     ( TFile           & file );
    bool                     FindNext                      ( TFile           & file );
                            ~CFileSystem                   ( void );
  //-----------------------------------------------------------------------------------------------
  private:
    static const uint32_t    FS_INVALID                    = UINT32_MAX;

                             CFileSystem                   ( uint32_t          sectors,
                                                             const TBlkDev   & dev );
                             CFileSystem                   ( const TBlkDev   & dev );
    bool                     serialize                     ( const void      * data,
                                                             size_t            size,
                                                             uint32_t          sectorNum ); 
    bool                     saveMetaData                  ( void );
    bool                     loadMetaData                  ( void );
    uint32_t                 findByName                    ( const char      * fileName );
    uint32_t                 findFreeSector                ( void );
    uint32_t                 makeFreeSectors               ( uint32_t          toFree );
    uint32_t                 lastSector                    ( uint32_t          firstSector );
    void                     truncate                      ( uint32_t          fileIdx );
    uint32_t                 createNewFile                 ( const char      * fileName );
    uint32_t                 openFile                      ( bool              writeMode,
                                                             uint32_t          fileIdx );
    size_t                   writeSector                   ( uint32_t          sector,
                                                             uint32_t          sectorOffset,
                                                             const void *      add,
                                                             size_t            addLen );
    size_t                   readSector                    ( uint32_t          sector,
                                                             uint32_t          sectorOffset,
                                                             void *            mem,
                                                             size_t            len );
    bool                     deserialize                   ( void            * data,
                                                             size_t            size,
                                                             uint32_t          sectorNum );                                               
    //---------------------------------------------------------------------------------------------
    TBlkDev                  m_BlkDev;

    uint32_t                 m_DataStartSector;
    uint32_t                 m_FirstFreeSector;
    uint32_t                 m_LastFreeSector;
    TFileEnt                 m_Files[DIR_ENTRIES_MAX];
    uint32_t               * m_FAT;
    TFileOpen                m_OpenFiles[OPEN_FILES_MAX];
    uint32_t                 m_Find = 0; // for the whole find first, find next mechanism 
};
//-------------------------------------------------------------------------------------------------
                             CFileSystem::CFileSystem      ( uint32_t          sectors,
                                                             const TBlkDev   & dev )
  : m_BlkDev ( dev ),
    m_DataStartSector ( (( DIR_ENTRIES_MAX * sizeof ( TFileEnt ) ) + SECTOR_SIZE -1)/ SECTOR_SIZE
                        + (( sectors * sizeof ( uint32_t ) + SECTOR_SIZE -1 )/ SECTOR_SIZE )
                        + 1 ), // FRAGILE
    m_FirstFreeSector ( m_DataStartSector ),
    m_LastFreeSector ( sectors - 1 ),
    m_FAT ( new uint32_t [sectors] )
{
  memset ( m_Files, 0, sizeof ( m_Files ) );
  
  for ( size_t i = 0; i < sectors; i++ )
  { 
    if ( i < m_DataStartSector || i == sectors - 1 )
      m_FAT[i] = FS_INVALID;
    else
      m_FAT[i] = i + 1;
  }

  for ( size_t i = 0; i < OPEN_FILES_MAX; i++ )
  { 
    m_OpenFiles[i] . m_IdxFiles = FS_INVALID;
  }
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::saveMetaData     ( void )
{
  THeader hdr;
  hdr . m_DataStartSector = m_DataStartSector;
  hdr . m_FirstFreeSector = m_FirstFreeSector;
  hdr . m_LastFreeSector  = m_LastFreeSector;
  memset ( hdr . m_PlaceHolder, 0, sizeof ( hdr . m_PlaceHolder ) );
  
  if ( serialize ( ( void * ) &hdr, sizeof ( THeader ), 0 ) != true )
    return false;

  if ( serialize ( ( void * ) m_Files, sizeof ( m_Files ), 1 )!= true )
    return false;

  if ( serialize ( ( void * ) m_FAT, 
              sizeof ( *m_FAT ) * m_BlkDev . m_Sectors,
              ( sizeof ( m_Files ) + SECTOR_SIZE - 1 )/ SECTOR_SIZE + 1 ) 
       != true )
    return false;
  return true;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::serialize        ( const void      * data,
                                                             size_t            size,
                                                             uint32_t          sectorNum )
{
  size_t    wholeSectors  = size / SECTOR_SIZE, leftOver = size % SECTOR_SIZE;
  uint8_t   mem[SECTOR_SIZE];

  if ( wholeSectors != 0 && m_BlkDev . m_Write ( sectorNum, data, wholeSectors ) != wholeSectors )
    return false;

  if ( leftOver )
  {
    sectorNum += wholeSectors;
    memset ( mem, 0, sizeof ( mem ) );
    memcpy ( mem, (const uint8_t*)data + wholeSectors * SECTOR_SIZE, leftOver );
    if ( m_BlkDev . m_Write ( sectorNum, mem , 1 ) != 1 )
      return false;
  }
  return true;
}
//-------------------------------------------------------------------------------------------------
                             CFileSystem::CFileSystem      ( const TBlkDev   & dev )
  : m_BlkDev ( dev )
{
  for ( size_t i = 0; i < OPEN_FILES_MAX; i++ )
    m_OpenFiles[i] . m_IdxFiles = FS_INVALID;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::loadMetaData     ( void )
{
  THeader hdr;
  m_BlkDev . m_Read ( 0, &hdr , 1 );

  m_DataStartSector = hdr . m_DataStartSector;
  m_FirstFreeSector = hdr . m_FirstFreeSector;
  m_LastFreeSector  = hdr . m_LastFreeSector;

  if ( ! deserialize ( m_Files, sizeof (m_Files), 1 ) )
    return false;

  m_FAT = new uint32_t[ m_BlkDev . m_Sectors ];

  if ( ! deserialize ( m_FAT, sizeof (*m_FAT) * m_BlkDev . m_Sectors, ( sizeof ( m_Files ) + SECTOR_SIZE - 1 ) / SECTOR_SIZE + 1  ) )
    return false;

  return true;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::deserialize      ( void            * data,
                                                             size_t            size,
                                                             uint32_t          sectorNum )
{
  size_t    wholeSectors  = size / SECTOR_SIZE, leftOver = size % SECTOR_SIZE;
  uint8_t   mem[SECTOR_SIZE];

  if (wholeSectors != 0 && m_BlkDev . m_Read ( sectorNum, data, wholeSectors ) != wholeSectors )
    return false;

  if ( leftOver )
  {
    if ( m_BlkDev . m_Read ( sectorNum + wholeSectors, mem, 1 ) != 1 )
      return false;
    memcpy ( (uint8_t *) data + wholeSectors * SECTOR_SIZE, mem, leftOver );
  }

  return true;
}
//-------------------------------------------------------------------------------------------------
                             CFileSystem::~CFileSystem     ( void )
{
  delete [] m_FAT;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::CreateFs         ( const TBlkDev   & dev )
{
  CFileSystem  hi ( dev . m_Sectors, dev );

  return hi . saveMetaData ();
}
//-------------------------------------------------------------------------------------------------
CFileSystem                * CFileSystem::Mount            ( const TBlkDev   & dev )
{
  CFileSystem * fs = new CFileSystem ( dev );
  fs -> loadMetaData ();
  return fs;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::Umount           ( void )
{
  return saveMetaData ();
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
size_t                       CFileSystem::FileSize         ( const char      * fileName )
{
  uint32_t idx = findByName ( fileName );

  if ( idx == FS_INVALID )
    return SIZE_MAX;
  
  return m_Files[idx] . m_FileSize;
}
//-------------------------------------------------------------------------------------------------
int                          CFileSystem::OpenFile         ( const char      * fileName,
                                                             bool              writeMode )
{
  size_t idx = findByName ( fileName );
  uint32_t fd;
  if ( writeMode == true )
  {
    if ( idx != FS_INVALID )
    {
      truncate ( idx );
    }
    else
    {
      idx = createNewFile ( fileName );
      if ( idx == FS_INVALID )
        return -1;
    }

    fd = openFile ( true, idx );
    if ( fd == FS_INVALID )
      return -1;
  }
  else // ReadMode
  {
    if ( idx != FS_INVALID )
    {
      fd = openFile ( false, idx ); // if opening file for read mode is somehow different deal with that!
      if ( fd == FS_INVALID )
        return -1;
    }
    else
      return -1;
  }

  return fd;
}
//-------------------------------------------------------------------------------------------------
void                         CFileSystem::truncate         ( uint32_t          fileIdx )
{
  // Gives the allocated sectors into free front, then it sets some meta data.
  makeFreeSectors ( m_Files[fileIdx] . m_FirstSector );

  m_Files[fileIdx] . m_FileSize = 0;
  m_Files[fileIdx] . m_FirstSector = FS_INVALID;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::createNewFile    ( const char      * fileName )
{
  for ( uint32_t i = 0; i < DIR_ENTRIES_MAX; ++i )
  {
    if ( m_Files[i] . m_FirstSector == 0 )
    {
      m_Files[i] . m_FirstSector = FS_INVALID;
      m_Files[i] . m_FileSize = 0;
      memset ( m_Files[i] . m_FileName, 0, FILENAME_LEN_MAX + 1 );
      strncpy ( m_Files[i] . m_FileName, fileName, FILENAME_LEN_MAX );
      return i;
    }
  }
  return FS_INVALID;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::openFile         ( bool              writeMode,
                                                             uint32_t          fileIdx )
{
  for ( uint32_t i = 0; i < OPEN_FILES_MAX; ++i )
  {
    if ( m_OpenFiles[i] . m_IdxFiles == FS_INVALID )
    {
      m_OpenFiles[i] . m_IdxFiles = fileIdx;
      m_OpenFiles[i] . m_Position = 0;
      m_OpenFiles[i] . m_Mode = writeMode;
      return i;
    }
  }
  return FS_INVALID;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::CloseFile        ( int               fd )
{
  if ( fd < 0 || fd >= OPEN_FILES_MAX )
    return false;
  
  m_OpenFiles[fd] . m_IdxFiles = FS_INVALID;
  m_OpenFiles[fd] . m_Position = 0;
  return true;
}
//-------------------------------------------------------------------------------------------------
size_t                       CFileSystem::ReadFile         ( int               fd,
                                                             void            * data,
                                                             size_t            len )
{
  if ( fd < 0 || fd >= OPEN_FILES_MAX )
    return 0;
  if ( len == 0 )
    return 0;

  TFileOpen  & of           = m_OpenFiles[fd]; 
  TFileEnt   & fe           = m_Files[of . m_IdxFiles];
  size_t       sectorNr     = of . m_Position / SECTOR_SIZE;
  size_t       sectorOffset = of . m_Position % SECTOR_SIZE;
  uint32_t     sector       = fe . m_FirstSector;

  for ( size_t i = 0; i < sectorNr && sector != FS_INVALID; sector = m_FAT[sector], i ++ ) { }

  if ( sector == FS_INVALID ) return 0;
  size_t res = 0;
  while ( res != len && of . m_Position < fe . m_FileSize )
  {
    if ( sector == FS_INVALID )
      break;

    size_t rd = len - res;
    if ( rd > SECTOR_SIZE - sectorOffset )
      rd = SECTOR_SIZE - sectorOffset;
    if ( of . m_Position + rd > fe . m_FileSize )
      rd = fe . m_FileSize - of . m_Position;

    readSector ( sector, sectorOffset, (uint8_t*)data + res, rd );
    res              += rd;
    of . m_Position  += rd;
    sector            = m_FAT[sector];
    sectorOffset      = 0;
  }
  return res;
}
//-------------------------------------------------------------------------------------------------
size_t                       CFileSystem::readSector       ( uint32_t          sector,
                                                             uint32_t          sectorOffset,
                                                             void *            mem,
                                                             size_t            len )
{
  uint8_t buffer[SECTOR_SIZE];

  size_t read = min ( SECTOR_SIZE - sectorOffset, len );
  m_BlkDev . m_Read ( sector, buffer, 1 );
  memcpy ( mem, buffer + sectorOffset, read );
  return read;
}
//-------------------------------------------------------------------------------------------------
size_t                       CFileSystem::WriteFile        ( int               fd,
                                                             const void      * data,
                                                             size_t            len )
{
  if ( fd < 0 || fd >= OPEN_FILES_MAX )
    return 0;
  
  TFileOpen  & of = m_OpenFiles[fd];
  TFileEnt   & fe = m_Files[of . m_IdxFiles];
  uint32_t     sectorToWrite = lastSector ( fe . m_FirstSector );


  size_t res = 0;
  while ( res != len )
  {
    if ( of . m_Position % SECTOR_SIZE == 0 )
    {
      sectorToWrite = findFreeSector();
      if ( sectorToWrite == FS_INVALID )
        break;

      if ( of . m_Position == 0 )
        fe . m_FirstSector = sectorToWrite;
      else
        m_FAT[lastSector(fe . m_FirstSector)] = sectorToWrite;

    }

    size_t wr = writeSector ( sectorToWrite, 
                       of . m_Position % SECTOR_SIZE,
                       (uint8_t *) data + res,
                       len - res );

    of . m_Position += wr;
    res             += wr;

  }
 
  fe . m_FileSize += res;
  return res;
}
//-------------------------------------------------------------------------------------------------
size_t                       CFileSystem::writeSector      ( uint32_t          sector,
                                                             uint32_t          sectorOffset,
                                                             const void *      add,
                                                             size_t            addLen )
{
  uint8_t buffer[SECTOR_SIZE];

  size_t written = min ( SECTOR_SIZE - sectorOffset, addLen );

  m_BlkDev . m_Read ( sector, buffer, 1 );
  memcpy ( buffer + sectorOffset, add, written );
  m_BlkDev . m_Write ( sector, buffer, 1 );

  return written;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::DeleteFile       ( const char      * fileName )
{
  // Theoretically I should deal with what happens when the deleted file is open, but I am not sure
  // what should happen, so I leave it be.
  // mm says: I think it should be possible to delete an open file and then any access to such
  //          a file (read/write) should report failure
  
  uint32_t idx = findByName ( fileName );

  if ( idx == FS_INVALID )
    return false;

  makeFreeSectors ( m_Files [idx] . m_FirstSector );
  
  m_Files [idx] . m_FirstSector = 0;
  m_Files [idx] . m_FileSize    = 0;
  return true;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::FindFirst        ( TFile           & file )
{
  for ( m_Find = 0; m_Find < DIR_ENTRIES_MAX; ++m_Find )
  {
    if ( m_Files[m_Find] . m_FirstSector != 0 )
    {
      strcpy (file . m_FileName, m_Files[m_Find] . m_FileName );
      file . m_FileSize = m_Files[m_Find] . m_FileSize;

      ++m_Find;
      return true;
    }
  }

  return false;
}
//-------------------------------------------------------------------------------------------------
bool                         CFileSystem::FindNext         ( TFile           & file )
{
  for ( ; m_Find < DIR_ENTRIES_MAX; ++m_Find )
  {
    if ( m_Files[m_Find]. m_FirstSector != 0 )
    {
      strcpy (file . m_FileName, m_Files[m_Find] . m_FileName ) ;
      file . m_FileSize = m_Files[m_Find] . m_FileSize;
      ++m_Find;
      return true;
    }
  }

  return false;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::findByName       ( const char      * fileName )
{
  for ( uint32_t i = 0; i < DIR_ENTRIES_MAX; i++ )
    if ( strncmp ( fileName, m_Files[i] . m_FileName, FILENAME_LEN_MAX ) == 0 )
      return i;
  return FS_INVALID;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::findFreeSector   ( void )
{
  if ( m_FirstFreeSector == FS_INVALID )
    return FS_INVALID;
  
  
  uint32_t free = m_FirstFreeSector;

  m_FirstFreeSector = m_FAT[free];
  m_FAT[free] = FS_INVALID;
  return free;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::makeFreeSectors  ( uint32_t          toFree )
{

  if ( toFree == FS_INVALID )
    return FS_INVALID;

  if ( m_FirstFreeSector == FS_INVALID )
  {
    m_FirstFreeSector = toFree;
    m_LastFreeSector = lastSector ( toFree );
    return m_FirstFreeSector;
  }

  
  uint32_t last = lastSector ( toFree );

  m_FAT[last] = m_FirstFreeSector;
  m_FirstFreeSector = toFree;

  return m_FirstFreeSector;
}
//-------------------------------------------------------------------------------------------------
uint32_t                     CFileSystem::lastSector       ( uint32_t          firstSector )
{
  if ( firstSector == FS_INVALID )
    return FS_INVALID;


  while ( m_FAT[firstSector] != FS_INVALID )
    firstSector = m_FAT[firstSector];

  return firstSector;
}
//=================================================================================================

#ifndef __PROGTEST__
#include "simple_test.inc"
#endif /* __PROGTEST__ */
