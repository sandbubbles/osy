#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */ 
//=================================================================================================
class CJob
{
  public:
                             CJob                          ( AShip             ship,
                                                             vector<CCargo>    cargo )
      : m_Ship  ( ship ),
        m_Cargo ( move ( cargo ) )
    {}

                             CJob                          ( AShip             ship )
      : m_Ship ( ship )
    {}

    AShip                    Ship                          ( void ) const
    {
      return m_Ship;
    }

    vector<CCargo>           Cargo                         ( void ) const
    {
      return m_Cargo;
    }

  private:
    AShip                    m_Ship;
    vector<CCargo>           m_Cargo;
};
typedef std::shared_ptr<CJob>  AJob;

//=================================================================================================
class CQueue
{
  
  public:
    void                     Push                          ( AJob              job );
    AJob                     Get                           ( void );
    size_t                   Size                          ( void ) const;           // because of the mutex cant be const 

  private:
    queue<AJob>              m_Queue;
    mutable mutex            m_Mutex;
    condition_variable       m_CV;
  
};
//-------------------------------------------------------------------------------------------------
void                         CQueue::Push                  ( AJob              job )
{
  unique_lock<mutex> ul ( m_Mutex );
  m_Queue . push ( job );
  ul . unlock ();
  m_CV . notify_all ( );
}
//-------------------------------------------------------------------------------------------------
AJob                         CQueue::Get                   ( void )
{

  unique_lock<mutex> ul ( m_Mutex );
  m_CV . wait ( ul, [ this ] () { return ( m_Queue . size () > 0 ); } );
  AJob first = m_Queue . front();

  if ( first )
    m_Queue . pop();

  return first;
}
//-------------------------------------------------------------------------------------------------
size_t                       CQueue::Size                  ( void ) const
{
  unique_lock<mutex> ul ( m_Mutex );
  return m_Queue . size ( );
}
//=================================================================================================
class CCargoPlanner
{
  public:
    static int               SeqSolver                     ( const vector<CCargo> & cargo,
                                                             int               maxWeight,
                                                             int               maxVolume,
                                                             vector<CCargo>  & load );
    void                     Start                         ( int               sales,
                                                             int               workers );
    void                     Stop                          ( void );

    void                     Customer                      ( ACustomer         customer );
    void                     Ship                          ( AShip             ship );

  private:
    vector<ACustomer>        m_Customers;
    vector<thread>           m_Workers;
    vector<thread>           m_Salesmen;
    CQueue                   m_WorkQue;
    CQueue                   m_SaleQue;

    void                     work                          ( );
    void                     sale                          ( );

};  

//-------------------------------------------------------------------------------------------------
int                          CCargoPlanner::SeqSolver      ( const vector<CCargo> & cargo,
                                                             int               maxWeight,
                                                             int               maxVolume,
                                                             vector<CCargo>  & load )
{
  return ProgtestSolver ( cargo, maxWeight, maxVolume, load );
}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::Start          ( int               sales,
                                                             int               workers )
{

  for ( int i = 0; i < sales; i ++ )
    m_Salesmen . push_back ( thread ( &CCargoPlanner::sale, this ) );

  for ( int i = 0; i < workers; i ++ )
    m_Workers . push_back ( thread ( &CCargoPlanner::work, this ) );
}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::Stop           ( void )
{
  m_SaleQue . Push ( NULL );

  for ( auto & t :  m_Salesmen )
    t . join ();

  m_WorkQue . Push ( NULL );

  for ( auto & t :  m_Workers )
    t . join ();

}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::Customer       ( ACustomer         customer )
{
  m_Customers . push_back ( customer );
}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::Ship           ( AShip             ship )
{
  m_SaleQue . Push ( make_shared<CJob> ( ship ) );

}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::work           ( void )
{
  while ( 1 )
  {
    AJob todo;
    todo = m_WorkQue . Get ();
    
    if ( !todo )
      return;
    
    AShip ship           = todo -> Ship ();
    vector<CCargo> cargo ( move ( todo -> Cargo () ) ) ;
    vector<CCargo> load;

    SeqSolver ( cargo, ship -> MaxWeight (), ship -> MaxVolume (), load );

    ship -> Load ( load );
  }

}
//-------------------------------------------------------------------------------------------------
void                         CCargoPlanner::sale           ( void )
{

  while ( 1 )
  {
    AJob todo;
    todo = m_SaleQue . Get ();

    if ( !todo )
      return;
    
    AShip ship = todo -> Ship ();
    vector<CCargo> cargo;
    string destination = ship -> Destination ();

    for ( const auto & customer : m_Customers )
    {
      vector<CCargo>         cargoTmp;
      customer -> Quote ( destination, cargoTmp );
      cargo . insert ( cargo . end (), cargoTmp . begin (), cargoTmp . end () );
    }

    m_WorkQue . Push ( make_shared<CJob> ( ship, cargo ) );
  }

}

//=================================================================================================
#ifndef __PROGTEST__
int                main                                    ( void )
{

  CCargoPlanner  test;
  vector<AShipTest> ships;
  vector<ACustomerTest> customers { make_shared<CCustomerTest> (), make_shared<CCustomerTest> () };
  
  ships . push_back ( g_TestExtra[0] . PrepareTest ( "New York", customers ) );
  ships . push_back ( g_TestExtra[1] . PrepareTest ( "Barcelona", customers ) );
  ships . push_back ( g_TestExtra[2] . PrepareTest ( "Kobe", customers ) );
  ships . push_back ( g_TestExtra[8] . PrepareTest ( "Perth", customers ) );
  
  
  for ( auto x : customers )
    test . Customer ( x );
  
  test . Start ( 3, 2 );
  
  for ( auto x : ships )
    test . Ship ( x );

  test . Stop  ();

  for ( auto x : ships )
    cout << x -> Destination () << ": " << ( x -> Validate () ? "ok" : "fail" ) << endl;

  return 0;  
}
#endif /* __PROGTEST__ */ 
