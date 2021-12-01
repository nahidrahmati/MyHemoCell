/*
This file is part of the HemoCell library

HemoCell is developed and maintained by the Computational Science Lab 
in the University of Amsterdam. Any questions or remarks regarding this library 
can be sent to: info@hemocell.eu

When using the HemoCell library in scientific work please cite the
corresponding paper: https://doi.org/10.3389/fphys.2017.00563

The HemoCell library is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

The library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "hemocell.h"
#include "rbcHighOrderModel.h"
#include "helper/hemoCellStretch.h"
#include "helper/cellInfo.h"


int main(int argc, char* argv[])
{   
	if(argc < 2)
	{
			cout << "Usage: " << argv[0] << " <configuration.xml>" << endl;
			return -1;
	}

	HemoCell hemocell(argv[1], argc, argv);
	Config * cfg = hemocell.cfg;
	
// ---------------------------- Calc. LBM parameters -------------------------------------------------
	hlog << "(CellStretch) (Parameters) calculating shear flow parameters" << endl;

  param::lbm_base_parameters(*cfg);

  param::ef_lbm = (*cfg)["parameters"]["stretchForce"].read<T>()*1e-12 / param::df;

  param::printParameters();
  
	// ------------------------ Init lattice --------------------------------

	plint nz = 13*(1e-6/(*cfg)["domain"]["dx"].read<T>());
  plint nx = 2*nz;
  plint ny = nz;

	hlog << "(CellStretch) Initializing lattice: " << nx <<"x" << ny <<"x" << nz << " [lu]" << std::endl;

	plint extendedEnvelopeWidth = 2;  // Because we might use ibmKernel with with 2.

	hemocell.lattice = new MultiBlockLattice3D<T,DESCRIPTOR>(
			defaultMultiBlockPolicy3D().getMultiBlockManagement(nx, ny, nz, extendedEnvelopeWidth),
			defaultMultiBlockPolicy3D().getBlockCommunicator(),
			defaultMultiBlockPolicy3D().getCombinedStatistics(),
			defaultMultiBlockPolicy3D().getMultiCellAccess<T, DESCRIPTOR>(),
			new GuoExternalForceBGKdynamics<T, DESCRIPTOR>(1.0/param::tau));


	hemocell.lattice->toggleInternalStatistics(false);
  hemocell.lattice->periodicity().toggleAll(false);

  OnLatticeBoundaryCondition3D<T,DESCRIPTOR>* boundaryCondition
          = createLocalBoundaryCondition3D<T,DESCRIPTOR>();

  boundaryCondition->setVelocityConditionOnBlockBoundaries(*hemocell.lattice);
  setBoundaryVelocity(*hemocell.lattice, hemocell.lattice->getBoundingBox(), plb::Array<T,3>(0.,0.,0.) );

  hemocell.latticeEquilibrium(1., hemo::Array<T, 3>({0.,0.,0.}));

	hemocell.lattice->initialize();

	// ----------------------- Init cell models --------------------------
	
	hemocell.initializeCellfield();
	hemocell.addCellType<RbcHighOrderModel>("RBC", RBC_FROM_SPHERE);
	vector<int> outputs = {OUTPUT_POSITION,OUTPUT_TRIANGLES,OUTPUT_FORCE,OUTPUT_FORCE_VOLUME,OUTPUT_FORCE_BENDING,OUTPUT_FORCE_LINK,OUTPUT_FORCE_AREA,OUTPUT_FORCE_VISC, OUTPUT_VERTEX_ID, OUTPUT_CELL_ID};
	hemocell.setOutputs("RBC", outputs);

	outputs = {OUTPUT_VELOCITY,OUTPUT_FORCE};
	hemocell.setFluidOutputs(outputs);
        
  hemocell.outputInSiUnits = true; //HDF5 output in SI units (except location (so fluid location, particle location is still in LU)

// ---------------------- Initialise particle positions if it is not a checkpointed run ---------------

	//loading the cellfield
  if (not cfg->checkpointed) {
    hemocell.loadParticles();
    hemocell.writeOutput();
  } else {
    hlog << "(CellStretch) CHECKPOINT found!" << endl;
    hemocell.loadCheckPoint();
  }

// Setting up the stretching
  unsigned int n_forced_lsps = 1 + 6;// + 12;
  HemoCellStretch cellStretch(*(*hemocell.cellfields)["RBC"],n_forced_lsps, param::ef_lbm);
  
  hlog << "(CellStretch) External stretching force [pN(flb)]: " <<(*cfg)["parameters"]["stretchForce"].read<T>() << " (" << param::ef_lbm  << ")" << endl;
    
  unsigned int tmax = (*cfg)["sim"]["tmax"].read<unsigned int>();
  unsigned int tmeas = (*cfg)["sim"]["tmeas"].read<unsigned int>();
  unsigned int tcheckpoint = (*cfg)["sim"]["tcheckpoint"].read<unsigned int>();

  // Get undeformed values
  T volume_lbm = (*hemocell.cellfields)["RBC"]->meshmetric->getVolume();
  T surface_lbm = (*hemocell.cellfields)["RBC"]->meshmetric->getSurface();
  T volume_eq = volume_lbm/pow(1e-6/param::dx,3);
  T surface_eq = surface_lbm/pow(1e-6/param::dx,2);

  hemo::Array<T,6> bb =  (*hemocell.cellfields)["RBC"]->getOriginalBoundingBox();
  hlog << "Original Bounding box:" << endl;
  hlog << "\tx: " << bb[0] << " : " << bb[1] << endl;
  hlog << "\ty: " << bb[2] << " : " << bb[3] << endl;
  hlog << "\tz: " << bb[4] << " : " << bb[5] << endl;
  
  // Creating output log file
  plb_ofstream fOut;
  if(cfg->checkpointed)
    fOut.open("stretch.log", std::ofstream::app);
  else
    fOut.open("stretch.log");


  while (hemocell.iter < tmax ) {  

    cellStretch.applyForce(); //IMPORTANT, not done normally in hemocell.iterate()
    hemocell.iterate();
    
    
    if (hemocell.iter % tmeas == 0) {
      hemocell.writeOutput();

      // Fill up the static info structure with desired data
      CellInformationFunctionals::calculateCellVolume(&hemocell);
      CellInformationFunctionals::calculateCellArea(&hemocell);
      CellInformationFunctionals::calculateCellPosition(&hemocell);
      CellInformationFunctionals::calculateCellStretch(&hemocell);
      CellInformationFunctionals::calculateCellBoundingBox(&hemocell);

      T volume = (CellInformationFunctionals::info_per_cell[0].volume)/pow(1e-6/param::dx,3);
      T surface = (CellInformationFunctionals::info_per_cell[0].area)/pow(1e-6/param::dx,2);
      hemo::Array<T,3> position = CellInformationFunctionals::info_per_cell[0].position/(1e-6/param::dx);
      hemo::Array<T,6> bbox = CellInformationFunctionals::info_per_cell[0].bbox/(1e-6/param::dx);
      T largest_diam = (CellInformationFunctionals::info_per_cell[0].stretch)/(1e-6/param::dx);

      hlog << "\t Cell center at: {" <<position[0]<<","<<position[1]<<","<<position[2] << "} µm" <<endl;  
      hlog << "\t Diameters: {" << bbox[1]-bbox[0] <<", " << bbox[3]-bbox[2] <<", " << bbox[5]-bbox[4] <<"}  µm" << endl;
      hlog << "\t Surface: " << surface << " µm^2" << " (" << surface / surface_eq * 100.0 << "%)" << "  Volume: " << volume << " µm^3" << " (" << volume / volume_eq * 100.0 << "%)"<<endl;
      hlog << "\t Largest diameter: " << largest_diam << " µm." << endl;
      
      fOut << hemocell.iter << " " << bbox[1]-bbox[0] << " " << bbox[3]-bbox[2] << " " << bbox[5]-bbox[4] << " " << volume / volume_eq * 100.0 << " " << surface / surface_eq * 100.0 << endl;

      CellInformationFunctionals::clear_list();
    }

    if (hemocell.iter % tcheckpoint == 0) {
      hemocell.saveCheckPoint();
    }
    
    
  }

  fOut.close();
  hlog << "(CellStretch) Simulation finished :)" << std::endl;

  return 0;
}
