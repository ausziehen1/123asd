#include "labels.h"
#include "store.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class Exception,class Fn>void raises(Fn fn){try{fn();}catch(const Exception&){return;}throw std::runtime_error("Expected exception missing");}
int main(int argc,char** argv){
    auto directory=std::filesystem::temp_directory_path()/("repair-labels-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        const auto base=std::string("http://192.168.10.20:8080");
        require(repair::normalize_public_url(base+"/")==base,"Public URL normalization");
        for(auto value:{"http://127.0.0.1:8080","http://localhost:8080","http://0.0.0.0:8080","http://[::1]:8080","http://server:0","https://server:8080","http://server:8080/edit"})
            raises<repair::ApiError>([&]{repair::normalize_public_url(value);});
        auto scan=repair::parse_scan(" ]Q3"+base+"/cards/123\r\n","http://localhost:8080");
        require(scan.id==123&&scan.base==base&&scan.url==base+"/cards/123","USB scanner URL and AIM prefix");
        require(repair::parse_scan("42","http://127.0.0.1:8080").url=="http://127.0.0.1:8080/cards/42","Manual internal ID");
        for(auto value:{"", "V00001","http://server:8080/edit/1","http://server:8080/cards/0","http://server:8080/cards/1?edit=1","file:///C:/x","99999999999999999999999999"})
            raises<repair::ApiError>([&]{repair::parse_scan(value,base);});
        repair::Json cards={{{"id",1},{"product_id","V00000024"},{"platform_name","Focusline 16"}},
                           {{"id",2},{"product_id","V00000024"},{"platform_name","Other platform"}}};
        auto labels=repair::make_labels(cards,base);
        require(labels.size()==2&&labels[0].product_id==labels[1].product_id&&labels[0].url!=labels[1].url,"Duplicate equipment IDs have different QR links");
        auto duplicate=cards;duplicate.push_back(cards[0]);
        raises<repair::ApiError>([&]{repair::make_labels(duplicate,base);});
        auto args=repair::bartender_arguments("C:/Labels/Repair cards.btw","C:/Jobs/job 1/labels.csv",false);
        require(args[0]=="/AF=C:/Labels/Repair cards.btw"&&args[1]=="/D=C:/Jobs/job 1/labels.csv","BarTender paths");
        require(args.size()==5&&args[3]=="/C=1"&&args[4]=="/S=1","Editing never starts printing");
        auto print=repair::bartender_arguments("a.btw","a.csv",true);
        require(print.size()==6&&print.back()=="/FPD","Print dialog requires user action");
        require(repair::quote_windows_argument(L"a b")==L"\"a b\"","Windows spaces quoting");
        require(repair::quote_windows_argument(L"a\"b")==L"\"a\\\"b\"","Windows quotes escaping");
        require(repair::quote_windows_argument(L"a\\")==L"\"a\\\\\"","Windows trailing slash escaping");
        std::filesystem::create_directories(directory);
        auto path=repair::write_label_job(directory,labels);
        require(std::filesystem::exists(path/"qr-1.svg")&&std::filesystem::exists(path/"qr-2.svg")&&std::filesystem::exists(path/"labels.csv"),"Complete label job");
        auto db=(directory/"test.sqlite3").u8string();
        {Store store(db);store.set_setting("public_url",base);store.set_setting("server_id","first",true);store.set_setting("server_id","second",true);
         require(store.setting("server_id")=="first","Server identity is stable");
         auto type=store.create_platform("Test").at("id").get<long long>();auto batch=store.create_batch(type,"V01",2,"template");
         auto selected=store.card_set({2,1});require(selected[0]["id"]==2&&selected[1]["id"]==1,"Selected card order");
         raises<NotFound>([&]{store.card_set({1,9999});});}
        {Store store(db);require(store.setting("public_url")==base,"QR address survives restart");}
        if(argc==3&&std::string(argv[1])=="--emit"){
            auto output=repair::write_label_job(std::filesystem::u8path(argv[2]),labels);
            std::cout<<output.u8string()<<'\n';
        }else std::cout<<"Label tests passed\n";
        std::filesystem::remove_all(directory);return 0;
    }catch(const std::exception& e){std::cerr<<"TEST FAILED: "<<e.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(directory,ignored);return 1;}
}
