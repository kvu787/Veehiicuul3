#include "Display.h"
#include <iostream>
#include <stdexcept>
int main()
{
    try
    {
        std::vector<Display> displays(3);
        displays[0].name=L"NE18NZ2"; displays[0].device=L"DisplayLaptop";
        displays[1].name=L"PA278QGV"; displays[1].device=L"DisplayMain"; displays[1].primary=true;
        displays[2].name=L"PA278QGV"; displays[2].device=L"DisplayOther";
        if(PrimaryDisplay(displays)!=1 || ResolveDisplay(displays,L"ne18nz2")!=0 || ResolveDisplay(displays,L"DisplayOther")!=2)
            throw std::runtime_error("Display policy selected an incorrect monitor.");
        bool rejected=false; try { (void)ResolveDisplay(displays,L"PA278QGV"); } catch(const std::exception&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Ambiguous monitor names were accepted.");
        displays[1].primary=false; rejected=false; try { (void)PrimaryDisplay(displays); } catch(const std::exception&) { rejected=true; }
        if(!rejected) throw std::runtime_error("A missing Windows main display was guessed.");
        std::cout << "PASS: dynamic primary flag, explicit test-display identity, duplicate-name rejection and missing-primary rejection. No GUI windows were created.\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what(); return 1; }
}
