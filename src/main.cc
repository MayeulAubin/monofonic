#include <cmath>
#include <complex>
#include <iostream>
#include <fstream>
#include <thread>

#include <unistd.h> // for unlink

#include <general.hh>
#include <grid_fft.hh>
#include <convolution.hh>

#include <transfer_function_plugin.hh>
#include <random_plugin.hh>
#include <output_plugin.hh>
#include <cosmology_calculator.hh>

// initialise with "default" values
namespace CONFIG{
int  MPI_thread_support = -1;
int  MPI_task_rank = 0;
int  MPI_task_size = 1;
bool MPI_ok = false;
bool MPI_threads_ok = false;
bool FFTW_threads_ok = false;
}

RNG_plugin *the_random_number_generator;
TransferFunction_plugin *the_transfer_function;
output_plugin *the_output_plugin;

int main( int argc, char** argv )
{
    csoca::Logger::SetLevel(csoca::LogLevel::Info);
    // csoca::Logger::SetLevel(csoca::LogLevel::Debug);

    // initialise MPI and multi-threading
#if defined(USE_MPI)
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &CONFIG::MPI_thread_support);
    CONFIG::MPI_threads_ok = CONFIG::MPI_thread_support >= MPI_THREAD_FUNNELED;
    MPI_Comm_rank(MPI_COMM_WORLD, &CONFIG::MPI_task_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &CONFIG::MPI_task_size);
    CONFIG::MPI_ok = true;

    // set up lower logging levels for other tasks
    if( CONFIG::MPI_task_rank!=0 )
    {
        csoca::Logger::SetLevel(csoca::LogLevel::Error);
    }
#endif

#if defined(USE_FFTW_THREADS)
  #if defined(USE_MPI)
    if (CONFIG::MPI_threads_ok)
        CONFIG::FFTW_threads_ok = FFTW_API(init_threads)();
  #else
    CONFIG::FFTW_threads_ok = FFTW_API(init_threads)();
  #endif 
#endif

#if defined(USE_MPI)
    FFTW_API(mpi_init)();
#endif
    
#if defined(USE_FFTW_THREADS)
    if (CONFIG::FFTW_threads_ok)
        FFTW_API(plan_with_nthreads)(std::thread::hardware_concurrency());
#endif

#if defined(USE_MPI)
    csoca::ilog << "MPI is enabled                : " << "yes (" << CONFIG::MPI_task_size << " tasks)" << std::endl;
#else
    csoca::ilog << "MPI is enabled                : " << "no" << std::endl;
#endif
    csoca::ilog << "MPI supports multi-threading  : " << (CONFIG::MPI_threads_ok? "yes" : "no") << std::endl;
    csoca::ilog << "Available HW threads / task   : " << std::thread::hardware_concurrency() << std::endl;
    csoca::ilog << "FFTW supports multi-threading : " << (CONFIG::FFTW_threads_ok? "yes" : "no") << std::endl;
#if defined(FFTW_MODE_PATIENT)
	csoca::ilog << "FFTW mode                     : FFTW_PATIENT" << std::endl;
#elif defined(FFTW_MODE_MEASURE)
    csoca::ilog << "FFTW mode                     : FFTW_MEASURE" << std::endl;
#else
	csoca::ilog << "FFTW mode                     : FFTW_ESTIMATE" << std::endl;
#endif
    //------------------------------------------------------------------------------
    // Parse command line options
    //------------------------------------------------------------------------------

    if (argc != 2)
    {
        // print_region_generator_plugins();
        print_TransferFunction_plugins();
        print_RNG_plugins();
        print_output_plugins();

        csoca::elog << "In order to run, you need to specify a parameter file!" << std::endl;
        exit(0);
    }

    
    //--------------------------------------------------------------------
    // Initialise parameters
    ConfigFile the_config(argv[1]);

    const size_t ngrid = the_config.GetValue<size_t>("setup", "GridRes");
    const real_t boxlen = the_config.GetValue<double>("setup", "BoxLength");
    const real_t zstart = the_config.GetValue<double>("setup", "zstart");
    const int LPTorder = the_config.GetValueSafe<double>("setup","LPTorder",100);
    const real_t astart = 1.0/(1.0+zstart);
    const real_t volfac(std::pow(boxlen / ngrid / 2.0 / M_PI, 1.5));

    const bool bDoFixing = false;

    //...
    const std::string fname_hdf5 = the_config.GetValueSafe<std::string>("output", "fname_hdf5", "output.hdf5");
    const std::string fname_analysis = the_config.GetValueSafe<std::string>("output", "fbase_analysis", "output");
    //////////////////////////////////////////////////////////////////////////////////////////////

    std::unique_ptr<CosmologyCalculator>  the_cosmo_calc;
    
    try
    {
        the_random_number_generator = select_RNG_plugin(the_config);
        the_transfer_function       = select_TransferFunction_plugin(the_config);
        the_output_plugin           = select_output_plugin(the_config);
        the_cosmo_calc = std::make_unique<CosmologyCalculator>(the_config, the_transfer_function);
    }catch(...){
        csoca::elog << "Problem during initialisation. See error(s) above. Exiting..." << std::endl;
        #if defined(USE_MPI) 
        MPI_Finalize();
        #endif
        return 1;
    }
    const real_t Dplus0 = the_cosmo_calc->CalcGrowthFactor(astart) / the_cosmo_calc->CalcGrowthFactor(1.0);
    const real_t vfac   = the_cosmo_calc->CalcVFact(astart);

    if( CONFIG::MPI_task_rank==0 )
    {
        // write power spectrum to a file
        std::ofstream ofs("input_powerspec.txt");
        for( double k=the_transfer_function->get_kmin(); k<the_transfer_function->get_kmax(); k*=1.1 ){
            ofs << std::setw(16) << k
                << std::setw(16) << std::pow(the_cosmo_calc->GetAmplitude(k, total) * Dplus0, 2.0)
                << std::setw(16) << std::pow(the_cosmo_calc->GetAmplitude(k, total), 2.0)
                << std::endl;
        }
    }

    // compute growth factors of the respective orders
    const double g1  = -Dplus0;
    const double g2  = (LPTorder>1)? -3.0/7.0*Dplus0*Dplus0 : 0.0;
    const double g3a = (LPTorder>2)? -1.0/3.0*Dplus0*Dplus0*Dplus0 : 0.0;
    const double g3b = (LPTorder>2)? 10.0/21.*Dplus0*Dplus0*Dplus0 : 0.0;
    const double g3c = (LPTorder>2)? -1.0/7.0*Dplus0*Dplus0*Dplus0 : 0.0;

    const double vfac1 =  vfac;
    const double vfac2 =  2*vfac1;
    const double vfac3 =  3*vfac1;

    //--------------------------------------------------------------------
    // Create arrays
    Grid_FFT<real_t> phi({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> phi2({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> phi3a({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> phi3b({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> A3x({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> A3y({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    Grid_FFT<real_t> A3z({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    
    std::array< Grid_FFT<real_t>*,3 > A3({&A3x,&A3y,&A3z});

    OrszagConvolver<real_t> Conv({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
    // NaiveConvolver<real_t> Conv({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});

    //--------------------------------------------------------------------
    // Some operators to add or subtract terms 
    auto assign_op = []( ccomplex_t res, ccomplex_t val ) -> ccomplex_t{ return res; };
    auto add_op = []( ccomplex_t res, ccomplex_t val ) -> ccomplex_t{ return val+res; };
    auto add2_op = []( ccomplex_t res, ccomplex_t val ) -> ccomplex_t{ return val+2.0*res; };
    auto sub_op = []( ccomplex_t res, ccomplex_t val ) -> ccomplex_t{ return val-res; };
    auto sub2_op = []( ccomplex_t res, ccomplex_t val ) -> ccomplex_t{ return val-2.0*res; };
    //--------------------------------------------------------------------
    
    //phi.FillRandomReal(6519);
    the_random_number_generator->Fill_Grid( phi );

    //======================================================================
    //... compute 1LPT displacement potential ....
    // phi = - delta / k^2
    double wtime = get_wtime();    
    csoca::ilog << "Computing phi(1) term..." << std::flush;
    phi.FourierTransformForward();
    
    phi.apply_function_k_dep([&](auto x, auto k) -> ccomplex_t {
        real_t kmod = k.norm();
        if( bDoFixing ) x = x / std::abs(x); //std::exp(ccomplex_t(0, iphase * PhaseRotation));
        else x = x;
        ccomplex_t delta = x * the_cosmo_calc->GetAmplitude(kmod, total);
        return -delta / (kmod * kmod) / volfac;
    });
    
    phi.zero_DC_mode();
    csoca::ilog << "   took " << get_wtime()-wtime << "s" << std::endl;
    
    //======================================================================
    //... compute 2LPT displacement potential ....
    
    wtime = get_wtime();    
    csoca::ilog << "Computing phi(2) term..." << std::flush;
    Conv.convolve_SumOfHessians( phi, {0,0}, phi, {1,1}, {2,2}, phi2, assign_op );
    Conv.convolve_Hessians( phi, {1,1}, phi, {2,2}, phi2, add_op );
    Conv.convolve_Hessians( phi, {0,1}, phi, {0,1}, phi2, sub_op );
    Conv.convolve_Hessians( phi, {0,2}, phi, {0,2}, phi2, sub_op );
    Conv.convolve_Hessians( phi, {1,2}, phi, {1,2}, phi2, sub_op );
    phi2.apply_InverseLaplacian();
    csoca::ilog << "   took " << get_wtime()-wtime << "s" << std::endl;

    //======================================================================
    //... compute 3LPT displacement potential
    
    //... 3a term ...
    wtime = get_wtime();    
    csoca::ilog << "Computing phi(3a) term..." << std::flush;
    Conv.convolve_Hessians( phi, {0,0}, phi, {1,1}, phi, {2,2}, phi3a, assign_op );
    Conv.convolve_Hessians( phi, {0,1}, phi, {0,2}, phi, {1,2}, phi3a, add2_op );
    Conv.convolve_Hessians( phi, {1,2}, phi, {1,2}, phi, {0,0}, phi3a, sub_op );
    Conv.convolve_Hessians( phi, {0,2}, phi, {0,2}, phi, {1,1}, phi3a, sub_op );
    Conv.convolve_Hessians( phi, {0,1}, phi, {0,1}, phi, {2,2}, phi3a, sub_op );
    phi3a.apply_InverseLaplacian();
    csoca::ilog << "   took " << get_wtime()-wtime << "s" << std::endl;
    
    //... 3b term ...
    wtime = get_wtime();    
    csoca::ilog << "Computing phi(3b) term..." << std::flush;
    Conv.convolve_SumOfHessians( phi, {0,0}, phi2, {1,1}, {2,2}, phi3b, assign_op );
    Conv.convolve_SumOfHessians( phi, {1,1}, phi2, {2,2}, {0,0}, phi3b, add_op );
    Conv.convolve_SumOfHessians( phi, {2,2}, phi2, {0,0}, {1,1}, phi3b, add_op );
    Conv.convolve_Hessians( phi, {0,1}, phi2, {0,1}, phi3b, sub2_op );
    Conv.convolve_Hessians( phi, {0,2}, phi2, {0,2}, phi3b, sub2_op );
    Conv.convolve_Hessians( phi, {1,2}, phi2, {1,2}, phi3b, sub2_op );
    phi3b.apply_InverseLaplacian();
    phi3b *= 0.5; // factor 1/2 from definition of phi(3b)!
    csoca::ilog << "   took " << get_wtime()-wtime << "s" << std::endl;

    //... transversal term ...
    wtime = get_wtime();    
    csoca::ilog << "Computing zeta(3) term..." << std::flush;
    for( int idim=0; idim<3; ++idim ){
        // cyclic rotations of indices
        int idimp = (idim+1)%3, idimpp = (idim+2)%3;
        Conv.convolve_Hessians( phi, {idim,idimp},  phi2, {idim,idimpp}, *A3[idim], assign_op );
        Conv.convolve_Hessians( phi, {idim,idimpp}, phi2, {idim,idimp},  *A3[idim], sub_op );
        Conv.convolve_DifferenceOfHessians( phi2, {idimp,idimpp}, phi,  {idimp,idimp}, {idimpp,idimpp}, *A3[idim], add_op );
        Conv.convolve_DifferenceOfHessians( phi,  {idimp,idimpp}, phi2, {idimp,idimp}, {idimpp,idimpp}, *A3[idim], sub_op );
        A3[idim]->apply_InverseLaplacian();
    }
    // Conv.convolve_Hessians( phi, {0,1}, phi2, {0,2}, *A3[0], assign_op );
    // Conv.convolve_Hessians( phi, {0,2}, phi2, {0,1}, *A3[0], sub_op );
    // Conv.convolve_DifferenceOfHessians( phi2, {1,2}, phi,  {1,1}, {2,2}, *A3[0], add_op );
    // Conv.convolve_DifferenceOfHessians( phi,  {1,2}, phi2, {1,1}, {2,2}, *A3[0], sub_op );
    // A3[0]->apply_InverseLaplacian();

    // Conv.convolve_Hessians( phi, {1,2}, phi2, {1,0}, *A3[1], assign_op );
    // Conv.convolve_Hessians( phi, {1,0}, phi2, {1,2}, *A3[1], sub_op );
    // Conv.convolve_DifferenceOfHessians( phi2, {2,0}, phi,  {2,2}, {0,0}, *A3[1], add_op );
    // Conv.convolve_DifferenceOfHessians( phi,  {2,0}, phi2, {2,2}, {0,0}, *A3[1], sub_op );
    // A3[1]->apply_InverseLaplacian();
    
    // Conv.convolve_Hessians( phi, {2,0}, phi2, {2,1}, *A3[2], assign_op );
    // Conv.convolve_Hessians( phi, {2,1}, phi2, {2,0}, *A3[2], sub_op );
    // Conv.convolve_DifferenceOfHessians( phi2, {0,1}, phi,  {0,0}, {1,1}, *A3[2], add_op );
    // Conv.convolve_DifferenceOfHessians( phi,  {0,1}, phi2, {0,0}, {1,1}, *A3[2], sub_op );
    // A3[2]->apply_InverseLaplacian();
    csoca::ilog << "   took " << get_wtime()-wtime << "s" << std::endl;

    ///... scale all potentials with respective growth factors
    phi *= g1;
    phi2 *= g2;
    phi3a *= g3a; 
    phi3b *= g3b;
    (*A3[0]) *= g3c;
    (*A3[1]) *= g3c;
    (*A3[2]) *= g3c;
    
    ///////////////////////////////////////////////////////////////////////
    // we store the densities here if we compute them
    const bool compute_densities = false;
    if( compute_densities ){
        Grid_FFT<real_t> delta({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        Grid_FFT<real_t> delta2({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        Grid_FFT<real_t> delta3a({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        Grid_FFT<real_t> delta3b({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        Grid_FFT<real_t> delta3({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        delta.FourierTransformForward(false);
        delta2.FourierTransformForward(false);
        delta3a.FourierTransformForward(false);
        delta3b.FourierTransformForward(false);
        delta3.FourierTransformForward(false);

        #pragma omp parallel for
        for (size_t i = 0; i < phi.size(0); ++i)
        {
            for (size_t j = 0; j < phi.size(1); ++j)
            {
                for (size_t k = 0; k < phi.size(2); ++k)
                {
                    auto kk = phi.get_k<real_t>(i,j,k);
                    size_t idx = phi.get_idx(i,j,k);
                    auto laplace = -kk.norm_squared();

                    // compute densities associated to respective potentials as well
                    delta.kelem(idx) = laplace * phi.kelem(idx);
                    delta2.kelem(idx) = laplace * phi2.kelem(idx);
                    delta3a.kelem(idx) = laplace * phi3a.kelem(idx);
                    delta3b.kelem(idx) = laplace * phi3b.kelem(idx);
                    delta3.kelem(idx) = delta3a.kelem(idx) + delta3b.kelem(idx);
                }
            }
        }

        delta.Write_PowerSpectrum(fname_analysis+"_"+"power_delta1.txt");
        delta2.Write_PowerSpectrum(fname_analysis+"_"+"power_delta2.txt");
        delta3a.Write_PowerSpectrum(fname_analysis+"_"+"power_delta3a.txt");
        delta3b.Write_PowerSpectrum(fname_analysis+"_"+"power_delta3b.txt");
        delta3.Write_PowerSpectrum(fname_analysis+"_"+"power_delta3.txt");

        phi.FourierTransformBackward();
        phi2.FourierTransformBackward();
        phi3a.FourierTransformBackward();
        phi3b.FourierTransformBackward();

        delta.FourierTransformBackward();
        delta2.FourierTransformBackward();
        delta3a.FourierTransformBackward();
        delta3b.FourierTransformBackward();
        delta3.FourierTransformBackward();

        A3[0]->FourierTransformBackward();
        A3[1]->FourierTransformBackward();
        A3[2]->FourierTransformBackward();

#if defined(USE_MPI)
        if( CONFIG::MPI_task_rank == 0 )
            unlink(fname_hdf5.c_str());
        MPI_Barrier( MPI_COMM_WORLD );
#else
        unlink(fname_hdf5.c_str());
#endif

        phi.Write_to_HDF5(fname_hdf5, "phi");
        phi2.Write_to_HDF5(fname_hdf5, "phi2");
        phi3a.Write_to_HDF5(fname_hdf5, "phi3a");
        phi3b.Write_to_HDF5(fname_hdf5, "phi3b");
        
        delta.Write_to_HDF5(fname_hdf5, "delta");
        delta2.Write_to_HDF5(fname_hdf5, "delta2");
        delta3a.Write_to_HDF5(fname_hdf5, "delta3a");
        delta3b.Write_to_HDF5(fname_hdf5, "delta3b");
        delta3.Write_to_HDF5(fname_hdf5, "delta3");

        A3[0]->Write_to_HDF5(fname_hdf5, "A3x");
        A3[1]->Write_to_HDF5(fname_hdf5, "A3y");
        A3[2]->Write_to_HDF5(fname_hdf5, "A3z");

    }else{
        // we store displacements and velocities here if we compute them
        


        // #pragma omp parallel for
        // for (size_t i = 0; i < phi.size(0); ++i) {
        //     for (size_t j = 0; j < phi.size(1); ++j) {
        //         for (size_t k = 0; k < phi.size(2); ++k) {
        //             auto kk = phi.get_k<real_t>(i,j,k);
        //             size_t idx = phi.get_idx(i,j,k);
                    
        //             auto phitot = phi.kelem(idx) + phi2.kelem(idx) + phi3a.kelem(idx) + phi3b.kelem(idx);
        //             auto phitot_v = vfac1 * phi.kelem(idx) + vfac2 * phi2.kelem(idx) + vfac3 * (phi3a.kelem(idx) + phi3b.kelem(idx));

        //             // divide by Lbox, because displacement is in box units for output plugin
        //             int idimp = (idim+1)%3, idimpp = (idim+1)%3;
        //             Psix.kelem(idx) = ccomplex_t(0.0,1.0) * (kk[0] * phitot + kk[1] * A3z.kelem(idx) - kk[2] * A3y.kelem(idx) ) / boxlen;
        //             Psiy.kelem(idx) = ccomplex_t(0.0,1.0) * (kk[1] * phitot + kk[2] * A3x.kelem(idx) - kk[0] * A3z.kelem(idx) ) / boxlen;
        //             Psiz.kelem(idx) = ccomplex_t(0.0,1.0) * (kk[2] * phitot + kk[0] * A3y.kelem(idx) - kk[1] * A3x.kelem(idx) ) / boxlen;

        //             Vx.kelem(idx)   = ccomplex_t(0.0,1.0) * (kk[0] * phitot_v + vfac3 * (kk[1] * A3z.kelem(idx) - kk[2] * A3y.kelem(idx)) ) / boxlen;
        //             Vy.kelem(idx)   = ccomplex_t(0.0,1.0) * (kk[1] * phitot_v + vfac3 * (kk[2] * A3x.kelem(idx) - kk[0] * A3z.kelem(idx)) ) / boxlen;
        //             Vz.kelem(idx)   = ccomplex_t(0.0,1.0) * (kk[2] * phitot_v + vfac3 * (kk[0] * A3y.kelem(idx) - kk[1] * A3x.kelem(idx)) ) / boxlen;
        //         }
        //     }
        // }

        Grid_FFT<real_t> tmp({ngrid, ngrid, ngrid}, {boxlen, boxlen, boxlen});
        
        // write out positions
        for( int idim=0; idim<3; ++idim ){
            // cyclic rotations of indices
            int idimp = (idim+1)%3, idimpp = (idim+2)%3;

            tmp.FourierTransformForward(false);

            #pragma omp parallel for
            for (size_t i = 0; i < phi.size(0); ++i) {
                for (size_t j = 0; j < phi.size(1); ++j) {
                    for (size_t k = 0; k < phi.size(2); ++k) {
                        auto kk = phi.get_k<real_t>(i,j,k);
                        size_t idx = phi.get_idx(i,j,k);
                        
                        auto phitot = phi.kelem(idx) + phi2.kelem(idx) + phi3a.kelem(idx) + phi3b.kelem(idx);
                        
                        // divide by Lbox, because displacement is in box units for output plugin
                        tmp.kelem(idx) = ccomplex_t(0.0,1.0) * (kk[idim] * phitot + kk[idimp] * A3[idimpp]->kelem(idx) - kk[idimpp] * A3[idimp]->kelem(idx) ) / boxlen;
                    }
                }
            }
            tmp.FourierTransformBackward();
            the_output_plugin->write_dm_position(idim, tmp );
        }

        // write out velocities
        for( int idim=0; idim<3; ++idim ){
            // cyclic rotations of indices
            int idimp = (idim+1)%3, idimpp = (idim+2)%3;

            tmp.FourierTransformForward(false);

            #pragma omp parallel for
            for (size_t i = 0; i < phi.size(0); ++i) {
                for (size_t j = 0; j < phi.size(1); ++j) {
                    for (size_t k = 0; k < phi.size(2); ++k) {
                        auto kk = phi.get_k<real_t>(i,j,k);
                        size_t idx = phi.get_idx(i,j,k);
                        
                        auto phitot_v = vfac1 * phi.kelem(idx) + vfac2 * phi2.kelem(idx) + vfac3 * (phi3a.kelem(idx) + phi3b.kelem(idx));

                        // divide by Lbox, because displacement is in box units for output plugin
                        tmp.kelem(idx) = ccomplex_t(0.0,1.0) * (kk[idim] * phitot_v + vfac3 * (kk[idimp] * A3[idimpp]->kelem(idx) - kk[idimpp] * A3[idimp]->kelem(idx)) ) / boxlen;
                    }
                }
            }
            tmp.FourierTransformBackward();
            the_output_plugin->write_dm_velocity(idim, tmp );
        }

        the_output_plugin->write_dm_mass(tmp);
        the_output_plugin->write_dm_density(tmp);
        
        the_output_plugin->finalize();
		delete the_output_plugin;
    }


    ///////////////////////////////////////////////////////////////////////


#if defined(USE_MPI)
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
#endif

    return 0;
}
