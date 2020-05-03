#include <iostream>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

enum datatype{DIR, INT_ARR, DOUBLE_ARR, BOOL_ARR, INT, DOUBLE, STRING, BOOL, UNDEF};

class A {

private:
   std::vector<std::pair<std::string,A>> _elem;
   const datatype type;
   int intmember;
   double doublemember;
   bool boolmember;
   std::string stringmember;
   std::vector<int> intvec;
   std::vector<double> doublevec;
   std::vector<bool> boolvec;

public:

   A(const A& a) : type{a.type} {
      intmember = a.intmember;
      doublemember = a.doublemember;
      boolmember = a.boolmember;
      stringmember = a.stringmember;
//      intvec = a.intvec;
//      doublevec = a.doublevec;
//      boolvec = a.boolvec;
   }

   A operator=(A&& a) = delete;

   // subdirectory
   A(std::initializer_list<std::pair<std::string,A>> list):type(DIR){
      for(auto x : list)
         _elem.push_back(x);
   }

   // Array
   A(std::initializer_list<int> list):type(INT_ARR){
      for(auto & element : list){
         intvec.push_back(element);
      }
   }

   A(std::initializer_list<double> list):type(DOUBLE_ARR){
      for(auto & element : list){
         doublevec.push_back(element);
      }
   }

   A(std::initializer_list<bool> list):type(BOOL_ARR){
      for(auto & element : list){
         boolvec.push_back(element);
      }
   }

   A(int leaf):type(INT),intmember(leaf){
   }
   A(double leaf):type(DOUBLE),doublemember(leaf){
   }
   A(bool leaf):type(BOOL),boolmember(leaf){
   }
   A(const char *leaf):type(STRING),stringmember(leaf){
   }

   void print(int level = 0){
      switch(type){
         case DIR:
            std::cout << std::endl;
            for(int i=0 ; i<_elem.size() ; i++){
               for(int i=0; i < level; i++)
                  std::cout << "   ";
               std::cout << _elem[i].first << ": ";
               _elem[i].second.print(level+1);
            }
            break;
         case INT:
            std::cout << intmember << std::endl;
            break;
         case DOUBLE:
            std::cout << doublemember << std::endl;
            break;
         case STRING:
            std::cout << "\"" << stringmember << "\"" << std::endl;
            break;
         case BOOL:
            std::cout << boolmember << std::endl;
            break;
         case INT_ARR:
            for(auto & x : intvec)
               std::cout << x << " ";
            std::cout<< std::endl;
            break;
         case DOUBLE_ARR:
            for(auto & x : doublevec)
               std::cout << x << " ";
            std::cout<< std::endl;
            break;
         case BOOL_ARR:
            for(auto  x : boolvec)
               std::cout << x << " ";
            std::cout<< std::endl;
            break;
         case UNDEF:
            break;
      }
   };
};


int main(){

   A obj = {
       {"number", 13},
       {"value", 17.5},
       {"string", "Wuzelgemuese"},
       {"mysubdir", {
          {"numberinsub",7},
          {"boolinsub",false},
          {"arrayinsub", {0.3,0.5,0.7}},
          {"subsubdir", {
             {"intinsubsub", 3},
             {"boolinsubsub", false},
             {"arrayinsubsub", {0.6,0.7,0.8}},
          }}
       }},
       {"array", {1,2,3,4,5}}
   };

   obj.print();
}
