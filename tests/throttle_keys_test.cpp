#include "throttle_keys.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

namespace {
void u32(std::vector<unsigned char>& d, std::uint32_t v) { for (int i=0;i<4;++i)d.push_back(static_cast<unsigned char>(v>>(i*8))); }
void text(std::vector<unsigned char>& d,const std::string& v){u32(d,static_cast<std::uint32_t>(v.size()+1));d.insert(d.end(),v.begin(),v.end());d.push_back(0);}
void binding(std::vector<unsigned char>& d,const char* property,const char* key){text(d,property);d.insert(d.end(),12,0);text(d,std::string("EOptionsKeys::")+key);d.insert(d.end(),8,0);}
}

int main(int argc, char** argv) {
    using namespace ac8throttle::keys;
    std::vector<unsigned char> data{'G','V','A','S'};
    binding(data,"ThrottleUpKey","Up");
    binding(data,"ThrottleDownKey","Down");
    binding(data,"ThrottleUpDownKey","LeftShift");
    const auto parsed=parse_save(data.data(),data.size());
    assert(parsed.found[accelerate]&&parsed.code[accelerate]==38);
    assert(parsed.found[decelerate]&&parsed.code[decelerate]==40);
    assert(parsed.found[combined]&&parsed.code[combined]==160);
    assert(virtual_key("F24")==135&&virtual_key("None")==0&&virtual_key("UnknownFutureKey")==-1);
    auto values=defaults();values[combined]='W';remove_combined_conflicts(values);assert(values[combined]==0);
    if(argc==2){std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> actual((std::istreambuf_iterator<char>(file)),{});const auto live=parse_save(actual.data(),actual.size());std::cout<<"live";for(int i=0;i<binding_count;++i)std::cout<<" "<<save_name(i)<<"="<<(live.found[i]?live.name[i]:"?");std::cout<<"\n";}
    std::cout<<"PASS throttle game-key parser\n";
}
