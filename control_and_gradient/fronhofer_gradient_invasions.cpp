//============================================================================
// Emanuel A. Fronhofer, Nicolai Nitsche & Florian Altermatt
// Information use shapes range expansion dynamics into environmental gradients
// Global Ecology and Biogepgraphy
// 2016
//
// control and gradient scenario
//============================================================================

/*
	Copyright (C) 2016  Emanuel A. Fronhofer

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	
*/

#include <iostream>
#include <cstdlib>								//standard C library
#include <ctime>								//access system time library
#include <fstream>								//file streaming library
#include <string>								//string library included
#include <sstream>								//string streaming for reading numbers from

#include <vector>
#include <cmath>								//standard math library

#include <gsl/gsl_rng.h>						//gsl random number generator
#include <gsl/gsl_randist.h>					//gsl random distributions
#include <gsl/gsl_statistics.h>					//gsl some statistical methods
#include <gsl/gsl_statistics_double.h> 			//gsl some double statistical methods
#include <gsl/gsl_sort_double.h> 				//gsl sort double arrays

#include <algorithm>

using namespace std;

#include "include/procedures.h"					//procedure simplifications
#include "include/classes.h"					//class definitions

//_____________________________________________________________________________
//------------------------------------------------------------ global variables
unsigned int sim_time;															// actual time in simulation
unsigned int burn_in;															// actual time in simulation
int max_runs;																	// no. of repeats
float mut_sd;																	// variance used for mutations
float mut_rate;																	// mutation rate
float alpha0;																	// intraspecific competition coefficient
float lambda_null;																// fertility
int equilibrium_density;														// density at population equilibrium
float mu0;																		// dispersal mortality
float epsilon;																	// random patch extinctions
float lamb_exp;																	// exponent of the lambda-alpha correlation
float trade_off_exp;															// dispersal-fertility trade-off exponent

TPatch world[WORLDDIM_X];														// simulated world

float rel_metapopsize;															// relative metapopulation size
float occupancy;																// metapopulation occupancy
float rel_emigrants_core;														// relative number of emigrants in the core
float rel_emigrants_margin;														// relative number of emigrants in the core
int margin_position;															// position of the range margin

bool mort_grad;																	// mortality gradient switch

//_____________________________________________________________________________
//------------------------------------------------------------------ procedures

//------------------------------------------------------------- read parameters
void readParameters(){
	ifstream parinfile("input/parameters.in");							//parameter input file
	string buffer;
	istringstream is;

	getline(parinfile,buffer); getline(parinfile,buffer);
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> sim_time;																		//simulation time
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> burn_in;																		//burn in time
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> max_runs;																		//no. of repeats
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> mut_sd;																		//variance used for mutations
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> mut_rate;																		//mutation rate
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> alpha0;																		//intraspecific competition coefficient
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> lamb_exp;																		//exponent of the lambda alpha correlation function
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> lambda_null;																	//fertility
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> trade_off_exp;																//fertility-dispersal trade-off exponent
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> epsilon;																		//random patch extinction probability
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	is >> mu0;																			//dispersal mortality
	getline(parinfile,buffer); getline(parinfile,buffer); is.clear(); is.str(buffer);
	if (buffer=="yes") {mort_grad = true;} else {mort_grad = false;};					//mortality gradient

	parinfile.close();
}

//-------------------------------------------------------- dispersal reproduction trade-off
float lambda_disersal_tradeOff(float disp_rate){

	// we need to make sure that we take the phenotypic value, as mutations do not constrain dispersal rates to positive values!
	float act_disp_rate = disp_rate;
	if(act_disp_rate < 0){
		act_disp_rate = 0;
	}

	float lambda = lambda_null*exp(-trade_off_exp*act_disp_rate);
	return(lambda);
}

//-------------------------------------------------------- alpha-lambda correlation function
float alpha_lambda_correlation(float lambda){
	float alpha = alpha0*pow(lambda,float(lamb_exp));
	return(alpha);
}

//------------------------------------------------------- initialize simulation
void Initialize(){
	// initialize patches and individuals in patches
	for (int x = 0; x < WORLDDIM_X; ++x) {

		// clear the world
		world[x].females.clear();
		world[x].newFemales.clear();

		// calculate equilibrium density
		equilibrium_density = round((lambda_null-1)/alpha_lambda_correlation(lambda_null));

		// initialize only in the initialization arae
		if (x < INIT_ROWS) {
			// initialize individuals in this patch
			// females
			for (int f = 0; f < equilibrium_density; ++f) {
				TIndiv newfemale;
				newfemale.dispRate = ran();
				newfemale.lambda = lambda_disersal_tradeOff(newfemale.dispRate);
				newfemale.alpha = alpha_lambda_correlation(newfemale.lambda);
				world[x].females.push_back(newfemale);
			}
		}
	}
}

// ------------------------------------------------ analyze population dynamics
void Analyze(unsigned int acttime, int actrun){
	//reset metapopulation size and occupancy
	rel_metapopsize = 0;
	occupancy = 0;
	// reset margin position
	margin_position = 0;

	unsigned int numberoccupied = 0;
	unsigned int metapopsize = 0;

	for (int x = 0; x < WORLDDIM_X; ++x) {
		unsigned int localpopsize = world[x].females.size();
		metapopsize += localpopsize;
		if (localpopsize > 0) {
			++numberoccupied;
			if (margin_position < x) {
				margin_position = x;
			}
		}
	}
	// calculate occupancy
	occupancy = float(numberoccupied) / float(WORLDDIM_X);
	// calculate relative metapopulation size
	rel_metapopsize = float(metapopsize) / float(WORLDDIM_X*equilibrium_density);
}

// ---------------------------------------------------- save individual results
void saveResults(int actrun, int acttime){
	// output file: individuals
	stringstream outputindiv_path_stream;
	outputindiv_path_stream << "output/output_spatial_profile_run" << actrun << "_time_" << acttime << ".out";
	string outputindiv_path = outputindiv_path_stream.str();
	ofstream outputindiv(outputindiv_path.c_str());

	// headers
	outputindiv << "x" << "    " << "dispRate_mean" << "    " << "alpha_mean" << "    " << "lambda_mean" << "    " << "dispRate_median" << "    " << "alpha_median" << "    " << "lambda_median" << "    " << "popSize" << endl;
	for (int x = 0; x < WORLDDIM_X; ++x) {

		vector<double> all_dispRate;
		all_dispRate.clear();

		vector<double> all_alpha;
		all_alpha.clear();

		vector<double> all_lambda;
		all_lambda.clear();

		unsigned int local_pop_size = world[x].females.size();

		for (unsigned int f = 0; f < local_pop_size; ++f) {
			all_dispRate.push_back(world[x].females.at(f).dispRate);
			all_alpha.push_back(world[x].females.at(f).alpha);
			all_lambda.push_back(world[x].females.at(f).lambda);
		}

		outputindiv << x << "    " << mean(all_dispRate, local_pop_size) << "    " << mean(all_alpha, local_pop_size) << "    " << mean(all_lambda, local_pop_size) << "    " << median(all_dispRate, local_pop_size) << "    " << median(all_alpha, local_pop_size) << "    " << median(all_lambda, local_pop_size) << "    " << local_pop_size << endl;

	}
	// close indiv output file
	outputindiv.close();
}

// --------------------------------------------- find new patch during dispersal
int findNewPatch(int x, int act_worlddim_x){

	int res;
	int dir;

	// nearest neighbour dispersal (left or right)
	dir = floor(ran()*2);

	switch (dir) {
	case 0:
		res = x-1;
		break;
	case 1:
		res = x+1;
		break;
	default:
		cout << "Error in NND" << endl;
		break;
	}

	// behaviour at world limits
	if (act_worlddim_x == INIT_ROWS) {
		// torus in x direction only during burnin
		if (res == act_worlddim_x){
			res = 0;
		}
		if (res == -1){
			res = act_worlddim_x-1;
		}
	} else {
		// otherwise reset to worldlimit
		if (res == act_worlddim_x){
			res = act_worlddim_x-1;
		}
		if (res == -1){
			res = 0;
		}
	}

	return(res);
}

//-----------------------------------------------------apply mortality gradient
void Mortality_gradient (int act_worlddim_x){

	// apply the mortality gradient here

	// only apply gradient after burnin
	if (act_worlddim_x > INIT_ROWS) {
		for (int x = 0; x < act_worlddim_x; ++x) {

			// calculate local mortality
			float local_mortality = float(x)/float(WORLDDIM_X-1);

			// just to make sure: clear new females
			world[x].newFemales.clear();

			// if there are individuals
			if (world[x].females.size() > 0){
				//loop over all individuals
				for (unsigned int f = 0; f < world[x].females.size(); ++f) {
					// if this individual survives
					if (ran() > local_mortality) {
						world[x].newFemales.push_back(world[x].females.at(f));
					}
				}

				// now kill the "old" females
				world[x].females.clear();
				// copy new females that survived back to females
				for (unsigned int nf = 0; nf < world[x].newFemales.size(); ++nf) {
					world[x].females.push_back(world[x].newFemales.at(nf));
				}
				// now empty new females again
				world[x].newFemales.clear();

			}
		}
	}
}

// -------------------------------------------------------- dispersal procedure
void Dispersal(int act_worlddim_x){

	// core
	unsigned int no_emigrants_core = 0;
	unsigned int metapopsize_core = 0;
	rel_emigrants_core = 0;
	//margin
	unsigned int no_emigrants_margin = 0;
	unsigned int metapopsize_margin = 0;
	rel_emigrants_margin = 0;

	for (int x = 0; x < act_worlddim_x; ++x) {
		// counter for metapopsize in core
		if (x < CM_AREA){metapopsize_core += world[x].females.size();}
		if (x >= (margin_position - CM_AREA)){metapopsize_margin += world[x].females.size();}

		// start with females
		for (unsigned int f = 0; f < world[x].females.size(); ++f) {
			// should the individual disperse?
			if (ran() < world[x].females.at(f).dispRate){
				// this individual will disperse
				// increase counter
				if (x < CM_AREA){++no_emigrants_core;}
				if (x >= (margin_position - CM_AREA)){++no_emigrants_margin;}
				// check whether this emigrant survives the dispersal process
				if (ran() > mu0){
					// find new patch (global dispersal)
					int newPatch = findNewPatch(x, act_worlddim_x);
					// copy disperser into new patch
					TIndiv Disperser = world[x].females.at(f);
					world[newPatch].newFemales.push_back(Disperser);
				}
				// delete emigrant from natal patch
				world[x].females.at(f) = world[x].females.back();
				world[x].females.pop_back();
				// decrease female loop counter
				--f;
			}
		}
	}

	// now that dispersal is over, merge philopatrics and residents
	for (int x = 0; x < act_worlddim_x; ++x) {
		// first copy the females
		for (unsigned int f = 0; f < world[x].newFemales.size(); ++f) {
			world[x].females.push_back(world[x].newFemales.at(f));
		}
		// erase the "old" immigrants from newFemales
		world[x].newFemales.clear();
	}

	rel_emigrants_core = float(no_emigrants_core) / float(metapopsize_core);
	rel_emigrants_margin = float(no_emigrants_margin) / float(metapopsize_margin);

}

// ------------------------------------------------------------------ mutations
float mutate(float allele){
	if(ran()< mut_rate){
		float newallele = allele + gauss(mut_sd);
		return(newallele);
	} else {
		return(allele);
	}
}

// ------------------------------------------------------------ larval survival
float larvalSurvival(unsigned int x, float alpha_sum, unsigned int f){
	// following the r-alpha model (see e.g. work by Jonathan Levine)

	float survival = 1/(1+alpha_sum);												//survival-probability of newborns based on r-alpha model

	return(survival);
}

// --------------------------------------------------------------- reproduction
void Reproduction(int act_worlddim_x){
	for (int x = 0; x < act_worlddim_x; ++x) {
		// just to be sure: resize new females and males vectors
		world[x].newFemales.clear();

		// for each patch check whether there are females
		if (world[x].females.size() > 0) {
			float alpha_sum = 0;
			for (unsigned int f = 0; f < world[x].females.size(); ++f) {
				alpha_sum = alpha_sum + world[x].females.at(f).alpha;
			}

			for (unsigned int f = 0; f < world[x].females.size(); ++f) {
				// calculate number of offspring (without density regulation)
				float survival = larvalSurvival(x,alpha_sum, f);

				int no_offspring = poisson(world[x].females.at(f).lambda*survival);
				// loop over offspring
				for (int o = 0; o < no_offspring; ++o) {

					// females
					// initialize new individual
					TIndiv newOffspring;

					newOffspring.dispRate = mutate(world[x].females.at(f).dispRate);
					newOffspring.lambda = lambda_disersal_tradeOff(newOffspring.dispRate);
					newOffspring.alpha = alpha_lambda_correlation(newOffspring.lambda);

					// add new individual to new females vector
					world[x].newFemales.push_back(newOffspring);
				}
			}
		}
	}
}


// -------------------------------------------------- death of annual organisms
void Death(int act_worlddim_x){
	for (int x = 0; x < act_worlddim_x; ++x) {
		int local_offspring_no = world[x].newFemales.size();
		if (local_offspring_no > 0) {
			// now clear adult vectors
			world[x].females.clear();
			// include local patch extinctions
			if (ran() > epsilon){
				// now copy new females into females
				for (unsigned int nf = 0; nf < world[x].newFemales.size(); ++nf) {
					world[x].females.push_back(world[x].newFemales.at(nf));
				}
			}
			// clear new females vector
			world[x].newFemales.clear();
			// clear new males vector
		} else {
			world[x].females.clear();
		}
	}
}

//_____________________________________________________________________________
//------------------------------------------------------------------------ main

int main() {
	// random number generator
	//specify_rng(time(NULL));
	specify_rng(RS);

	//read parameters for all simulation runs
	readParameters();

	// repeat loop
	for (int actrun = 0; actrun < max_runs; ++actrun) {

		// output file: metapop
		stringstream outputmetapop_path_stream;
		outputmetapop_path_stream << "output/output_metapop_run" << actrun << ".out";
		string outputmetapop_path = outputmetapop_path_stream.str();
		ofstream outputmetapop(outputmetapop_path.c_str());

		// outputfile header
		outputmetapop << "time" << "    " << "rel_metapopsize" << "    " << "occupancy"<< "    " << "emirate_core" << "    " << "emirate_margin" << "    " << "margin_position" << endl;

		Initialize();

		// time loop
		for (unsigned int acttime = 0; acttime < sim_time; ++acttime) {

			// for burnin phase: restrict world dimension to this area (this is only applicable to x dim, as y dim is not changed)
			int act_worlddim_x = 0;

			if (acttime < burn_in) {
				act_worlddim_x = INIT_ROWS;
			} else {
				act_worlddim_x = WORLDDIM_X;
			}

			// apply local mortality gradient
			if (mort_grad == true) {
				Mortality_gradient(act_worlddim_x);
			}

			// natal dispersal
			Dispersal(act_worlddim_x);

			// reproduction
			Reproduction(act_worlddim_x);

			// density regulation and death of adults
			Death(act_worlddim_x);

			// analyze metapopulation
			Analyze(acttime, actrun);

			// write metapop results to file
			outputmetapop << acttime << "    " << rel_metapopsize << "    " << occupancy << "    " << rel_emigrants_core << "   " << rel_emigrants_margin << "   " << margin_position << endl;

			//end of simulation once the world is full
			if (margin_position == (WORLDDIM_X-1) || acttime == (sim_time-1)) {
				saveResults(actrun, acttime);
				break;
			}

		}
		// close metapop output file
		outputmetapop.close();
	}
	cout << "job done" << endl;
	return 0;
}
