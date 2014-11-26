#include <RAT/WatchmanDetectorFactory.hh>
#include <RAT/Log.hh>
#include <RAT/DB.hh>

#include <math.h>
#include <vector>

using namespace std;

namespace RAT {

    void WatchmanDetectorFactory::DefineDetector(DBLinkPtr detector) {
        const double photocathode_coverage = detector->GetD("photocathode_coverage");
        const std::string geo_template = "Watchman/Watchman.geo";
        DB *db = DB::Get();
        if (db->Load(geo_template) == 0) {
            Log::Die("WatchmanDetectorFactory: could not load template Watchman/Watchman.geo");
        }
        
        //calculate the area of the defined inner_pmts
        DBLinkPtr inner_pmts = db->GetLink("GEO","inner_pmts");
        string pmt_type = inner_pmts->GetS("pmt_type");
        DBLinkPtr pmt = db->GetLink("PMT", pmt_type);
        vector<double> rho_edge = pmt->GetDArray("rho_edge");
        double photocathode_radius = rho_edge[0];
        for (size_t i = 1; i < rho_edge.size(); i++) {
            if (photocathode_radius < rho_edge[i]) photocathode_radius = rho_edge[i];
        }
        const double photocathode_area = M_PI*photocathode_radius*photocathode_radius;
        
        DBLinkPtr shield = db->GetLink("GEO","shield");
        const double steel_thickness = shield->GetD("steel_thickness");
        const double shield_thickness = shield->GetD("shield_thickness");
        const double detector_size = shield->GetD("detector_size");
        
        const double cable_radius = detector_size/2.0 - shield_thickness + 4.0*steel_thickness;
        const double pmt_radius = detector_size/2.0 - shield_thickness - 4.0*steel_thickness;
        const double topbot_offset = detector_size/2.0 - shield_thickness;
        
        const double surface_area = 2.0*M_PI*pmt_radius*pmt_radius + 2.0*topbot_offset*2.0*M_PI*pmt_radius;
        const double required_pmts = ceil(photocathode_coverage * surface_area / photocathode_area);
        
        const double pmt_space = sqrt(surface_area/required_pmts);
        
        const size_t cols = round(2.0*M_PI*pmt_radius/pmt_space);
        const size_t rows = round(2.0*topbot_offset/pmt_space);
        
        info << "Generating new PMT positions for:\n";
        info << "\tdesired photocathode coverage " << photocathode_coverage << '\n';
        info << "\ttotal area " << surface_area << '\n';
        info << "\tphotocathode radius " << photocathode_radius << '\n';
        info << "\tphotocathode area " << photocathode_area << '\n';
        info << "\tdesired PMTs " << required_pmts << '\n';
        info << "\tPMT spacing " << pmt_space << '\n';
        
        //make the grid for top and bottom PMTs
        vector<pair<int,int> > topbot;
        const int rdim = round(pmt_radius/pmt_space); 
        for (int i = -rdim; i <= rdim; i++) {
            for (int j = -rdim; j <= rdim; j++) {
                if (pmt_space*sqrt(i*i+j*j) <= pmt_radius-pmt_space/2.0) {
                    topbot.push_back(make_pair(i,j));
                }
            }
        }
        
        size_t num_pmts = cols*rows + 2*topbot.size();
        
        info << "Actual calculated values:\n"; 
        info << "\tactual photocathode coverage " << photocathode_area*num_pmts/surface_area << '\n';
        info << "\tgenerated PMTs " << num_pmts << '\n';
        info << "\tcols " << cols << '\n';
        info << "\trows " << rows << '\n';
        
        //generate cylinder PMT positions
        vector<double> x(num_pmts), y(num_pmts), z(num_pmts), dir_x(num_pmts), dir_y(num_pmts), dir_z(num_pmts);
        vector<int> type(num_pmts);
        
        for (size_t col = 0; col < cols; col++) {
            for (size_t row = 0; row < rows; row++) {
                const size_t idx = row + col*rows;
                const double phi = 2.0*M_PI*(col+0.5)/cols;
                
                x[idx] = pmt_radius*cos(phi);
                y[idx] = pmt_radius*sin(phi);
                z[idx] = row*2.0*topbot_offset/rows + pmt_space/2.0 - topbot_offset;
                
                dir_x[idx] = -cos(phi);
                dir_y[idx] = -sin(phi);
                dir_z[idx] = 0.0;
                
                type[idx] = 1;
            }
        }
        
        //generate topbot PMT positions
        for (size_t i = 0; i < topbot.size(); i++) {
            const size_t idx = rows*cols+i*2;
            
            //top = idx
            x[idx] = pmt_space*topbot[i].first;
            y[idx] = pmt_space*topbot[i].second;
            z[idx] = topbot_offset;
            
            dir_x[idx] = dir_y[idx] = 0.0;
            dir_z[idx] = -1.0;
            
            type[idx] = 1;
            
            //bot = idx+1
            x[idx+1] = pmt_space*topbot[i].first;
            y[idx+1] = pmt_space*topbot[i].second;
            z[idx+1] = -topbot_offset;
            
            dir_x[idx+1] = dir_y[idx] = 0.0;
            dir_z[idx+1] = 1.0;
            
            type[idx+1] = 1;
        }
        
        //generate cable positions
        vector<double> cable_x(cols), cable_y(cols);
        for (size_t col = 0; col < cols; col++) {
            cable_x[col] = cable_radius*cos(col*2.0*M_PI/cols);
            cable_y[col] = cable_radius*sin(col*2.0*M_PI/cols);
        }
        
        info << "Override default PMTINFO information...\n";
        db->SetDArray("PMTINFO","x",x);
        db->SetDArray("PMTINFO","y",y);
        db->SetDArray("PMTINFO","z",z);
        db->SetDArray("PMTINFO","dir_x",dir_x);
        db->SetDArray("PMTINFO","dir_y",dir_y);
        db->SetDArray("PMTINFO","dir_z",dir_z);
        db->SetIArray("PMTINFO","type",type);
        
        info << "Disable veto_pmts for dynamic coverage...\n";
        db->SetI("GEO","veto_pmts","enable",0);
        db->SetI("GEO","shield","veto_start",0);
        db->SetI("GEO","shield","veto_len",0);
        
        info << "Update geometry fields related to normal PMTs...\n";
        db->SetI("GEO","shield","cols",cols);
        db->SetI("GEO","shield","rows",rows);
        db->SetI("GEO","shield","inner_start",0);
        db->SetI("GEO","shield","inner_len",num_pmts);
        db->SetI("GEO","inner_pmts","start_num",0);
        db->SetI("GEO","inner_pmts","max_pmts",num_pmts);
        
        info << "Update cable positions to match shield...\n";
        db->SetDArray("cable_pos","x",cable_x);
        db->SetDArray("cable_pos","y",cable_y);
        db->SetDArray("cable_pos","z",vector<double>(cols,0.0));
        db->SetDArray("cable_pos","dir_x",vector<double>(cols,0.0));
        db->SetDArray("cable_pos","dir_y",vector<double>(cols,0.0));
        db->SetDArray("cable_pos","dir_z",vector<double>(cols,1.0));
    }

}