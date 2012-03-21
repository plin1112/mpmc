/* 

@2007, Jonathan Belof
Space Research Group
Department of Chemistry
University of South Florida

*/

#include <mc.h>

/* the prime quantity of interest */
void boltzmann_factor(system_t *system, double initial_energy, double final_energy) {

	double delta_energy;
	double u, g, partfunc_ratio;
	double fugacity;
	double v_new, v_old;

	delta_energy = final_energy - initial_energy;

	if(system->h2_fugacity || system->co2_fugacity || system->ch4_fugacity || system->n2_fugacity)
		fugacity = system->fugacity;
	else
		fugacity = system->pressure;

	if(system->ensemble == ENSEMBLE_UVT) {

		/* if biased_move not set, no cavity available so do normal evaluation below */
		if(system->cavity_bias && system->checkpoint->biased_move) {

			/* modified metropolis function */
			if(system->checkpoint->movetype == MOVETYPE_INSERT) {		/* INSERT */
				system->nodestats->boltzmann_factor = (system->cavity_volume*system->avg_nodestats->cavity_bias_probability*fugacity*ATM2REDUCED/(system->temperature*system->observables->N))*exp(-delta_energy/system->temperature);
			} else if(system->checkpoint->movetype == MOVETYPE_REMOVE) {	/* REMOVE */
				system->nodestats->boltzmann_factor = (system->temperature*(system->observables->N + 1))/(system->cavity_volume*system->avg_nodestats->cavity_bias_probability*fugacity*ATM2REDUCED)*exp(-delta_energy/system->temperature);
			} else {							/* DISPLACE */
				system->nodestats->boltzmann_factor = exp(-delta_energy/system->temperature);
			}

		} else {

			if(system->checkpoint->movetype == MOVETYPE_INSERT) {		/* INSERT */
				system->nodestats->boltzmann_factor = (system->pbc->volume*fugacity*ATM2REDUCED/(system->temperature*system->observables->N))*exp(-delta_energy/system->temperature);
			} else if(system->checkpoint->movetype == MOVETYPE_REMOVE) {	/* REMOVE */
				system->nodestats->boltzmann_factor = (system->temperature*(system->observables->N + 1))/(system->pbc->volume*fugacity*ATM2REDUCED)*exp(-delta_energy/system->temperature);
			} else if(system->checkpoint->movetype == MOVETYPE_DISPLACE) {	/* DISPLACE */
				system->nodestats->boltzmann_factor = exp(-delta_energy/system->temperature);
			} else if(system->checkpoint->movetype == MOVETYPE_SPINFLIP) {	/* SPINFLIP */

				/* the molecular partition funcs for g/u symmetry species */
				g = system->checkpoint->molecule_altered->rot_partfunc_g;
				u = system->checkpoint->molecule_altered->rot_partfunc_u;

				/* determine which way the spin was flipped */
				if(system->checkpoint->molecule_altered->nuclear_spin == NUCLEAR_SPIN_PARA)             /* ortho -> para */
					partfunc_ratio = g/(g+u);
				else if (system->checkpoint->molecule_altered->nuclear_spin == NUCLEAR_SPIN_ORTHO)      /* para -> ortho */
					partfunc_ratio = u/(g+u);

				/* set the boltz factor, including ratio of partfuncs for different symmetry rotational levels */
				system->nodestats->boltzmann_factor = partfunc_ratio;

			}

		}

	} else if(system->ensemble == ENSEMBLE_NVT) {

		if(system->checkpoint->movetype == MOVETYPE_SPINFLIP) {	/* SPINFLIP */

			/* the molecular partition funcs for g/u symmetry species */
			g = system->checkpoint->molecule_altered->rot_partfunc_g;
			u = system->checkpoint->molecule_altered->rot_partfunc_u;

			/* determine which way the spin was flipped */
			if(system->checkpoint->molecule_altered->nuclear_spin == NUCLEAR_SPIN_PARA)             /* ortho -> para */
				partfunc_ratio = g/(g+u);
			else if (system->checkpoint->molecule_altered->nuclear_spin == NUCLEAR_SPIN_ORTHO)      /* para -> ortho */
				partfunc_ratio = u/(g+u);

			/* set the boltz factor, including ratio of partfuncs for different symmetry rotational levels */
			system->nodestats->boltzmann_factor = partfunc_ratio;

		} else	/* DISPLACE */
			system->nodestats->boltzmann_factor = exp(-delta_energy/system->temperature);



	} else if(system->ensemble == ENSEMBLE_NPT) {

		if ( system->checkpoint->movetype == MOVETYPE_VOLUME ) { /*shrink/grow simulation box*/
			v_old = system->checkpoint->observables->volume;
			v_new = system->observables->volume;
			//boltzmann factor for log volume changes
			system->nodestats->boltzmann_factor = 
				exp(-(		delta_energy 
							+ system->pressure * ATM2REDUCED * (v_new-v_old) 
							- (system->observables->N + 1) * system->temperature * log(v_new/v_old)
						 )/system->temperature);

		}

		else 	/* DISPLACE */
			system->nodestats->boltzmann_factor = exp(-delta_energy/system->temperature);



	} else if(system->ensemble == ENSEMBLE_NVE) {
		system->nodestats->boltzmann_factor = pow((system->total_energy - final_energy), 3.0*system->N/2.0);
		system->nodestats->boltzmann_factor /= pow((system->total_energy - initial_energy), 3.0*system->N/2.0);
	}


	return;
}

/* keep track of which specific moves were accepted */
void register_accept(system_t *system) {

	++system->nodestats->accept;
	switch(system->checkpoint->movetype) {

		case MOVETYPE_INSERT:
			++system->nodestats->accept_insert;
			break;
		case MOVETYPE_REMOVE:
			++system->nodestats->accept_remove;
			break;
		case MOVETYPE_DISPLACE:
			++system->nodestats->accept_displace;
			break;
		case MOVETYPE_ADIABATIC:
			++system->nodestats->accept_adiabatic;
			break;
		case MOVETYPE_SPINFLIP:
			++system->nodestats->accept_spinflip;
			break;
		case MOVETYPE_VOLUME:
			++system->nodestats->accept_volume;
			break;
	}

}

/* keep track of which specific moves were rejected */
void register_reject(system_t *system) {

	++system->nodestats->reject;
	switch(system->checkpoint->movetype) {

		case MOVETYPE_INSERT:
			++system->nodestats->reject_insert;
			break;
		case MOVETYPE_REMOVE:
			++system->nodestats->reject_remove;
			break;
		case MOVETYPE_DISPLACE:
			++system->nodestats->reject_displace;
			break;
		case MOVETYPE_ADIABATIC:
			++system->nodestats->reject_adiabatic;
			break;
		case MOVETYPE_SPINFLIP:
			++system->nodestats->reject_spinflip;
			break;
		case MOVETYPE_VOLUME:
			++system->nodestats->reject_volume;
			break;

	}

}

/* implements the Markov chain */
int mc(system_t *system) {

	int j, msgsize;
	double initial_energy, final_energy, delta_energy;
	observables_t *observables_mpi;
	avg_nodestats_t *avg_nodestats_mpi;
	char *snd_strct, *rcv_strct;
#ifdef MPI
	MPI_Datatype msgtype;
#endif /* MPI */


	/* allocate the statistics structures */
	observables_mpi = calloc(1, sizeof(observables_t));
	avg_nodestats_mpi = calloc(1, sizeof(avg_nodestats_t));
	if(!(observables_mpi && avg_nodestats_mpi)) {
		error("MC: couldn't allocate space for statistics\n");
		return(-1);
	}

	/* if MPI not defined, then compute message size like dis */
	msgsize = sizeof(observables_t) + sizeof(avg_nodestats_t);
	if(system->calc_hist)
		msgsize += system->n_histogram_bins*sizeof(int);
#ifdef MPI
	MPI_Type_contiguous(msgsize, MPI_BYTE, &msgtype);
	MPI_Type_commit(&msgtype);
#endif /* MPI */

	/* allocate MPI structures */
	snd_strct = calloc(msgsize, 1);
	if(!snd_strct) {
		error("MC: couldn't allocate MPI message send space!\n");
		return(-1);
	}

	if(!rank) {
		rcv_strct = calloc(size, msgsize);
		if(!rcv_strct) {
			error("MC: root couldn't allocate MPI message receive space!\n");
			return(-1);
		}
	}

	/* clear our statistics */
	clear_nodestats(system->nodestats);
	clear_node_averages(system->avg_nodestats);
	clear_observables(system->observables);
	clear_root_averages(system->avg_observables);

	/* update the grid for the first time */
	if(system->cavity_bias) cavity_update_grid(system);

	/* determine the initial number of atoms in the simulation */
	system->checkpoint->N_atom = num_atoms(system);
	system->checkpoint->N_atom_prev = system->checkpoint->N_atom;

	/* set volume observable */
	system->observables->volume = system->pbc->volume;

	/* get the initial energy of the system */
	initial_energy = energy(system);

#ifdef QM_ROTATION
	/* solve for the rotational energy levels */
	if(system->quantum_rotation) quantum_system_rotational_energies(system);
#endif /* QM_ROTATION */

	/* be a bit forgiving of the initial state */
	if(!finite(initial_energy)) {
		initial_energy = MAXVALUE;
		system->observables->energy = MAXVALUE;
	}

	/* set the initial values */
	track_ar(system->nodestats);
	update_nodestats(system->nodestats, system->avg_nodestats);
	update_root_averages(system, system->observables, system->avg_nodestats, system->avg_observables);

	/* if root, open necessary output files */
	if(!rank) {

		output("MC: initial values:\n");
		write_averages(system);
		if(open_files(system) < 0) {
			error("MC: could not open files\n");
			return(-1);
		}

	}

	if ( system->ensemble == ENSEMBLE_TE ) //per Chris's request that energy_output be written for TE runs.
		write_observables(system->file_pointers.fp_energy, system->observables);

	/* save the initial state */
	checkpoint(system);

	/* main MC loop */
	for(system->step = 1; system->step <= system->numsteps; (system->step)++) {

		/* restore the last accepted energy */
		initial_energy = system->observables->energy;

		/* perturb the system */
		make_move(system);

		/* calculate the energy change */
		final_energy = energy(system);
		delta_energy = final_energy - initial_energy;

#ifdef QM_ROTATION
		/* solve for the rotational energy levels */
		if(system->quantum_rotation && (system->checkpoint->movetype == MOVETYPE_SPINFLIP))
			quantum_system_rotational_energies(system);
#endif /* QM_ROTATION */

		/* treat a bad contact as a reject */
		if(!finite(final_energy)) {
			system->observables->energy = MAXVALUE;
			system->nodestats->boltzmann_factor = 0;
		} else boltzmann_factor(system, initial_energy, final_energy);

		/* Metropolis function */
		if(get_rand() < system->nodestats->boltzmann_factor) {	/* ACCEPT */

			/* checkpoint */
			checkpoint(system);
			register_accept(system);

			/* SA */
			if(system->simulated_annealing) system->temperature *= system->simulated_annealing_schedule;

		} else {						/* REJECT */

			/* restore from last checkpoint */
			restore(system);
			register_reject(system);

		}

		/* track the acceptance_rate */
		track_ar(system->nodestats);

		/* each node calculates its stats */
		update_nodestats(system->nodestats, system->avg_nodestats);

		/* do this every correlation time, and at the very end */
		if(!(system->step % system->corrtime) || (system->step == system->numsteps)) {



			/* copy observables and avgs to the mpi send buffer */
			/* histogram array is at the end of the message */
			if(system->calc_hist) {
				zero_grid(system->grids->histogram->grid,system);
				population_histogram(system);
			}

			/* zero the send buffer */
			memset(snd_strct, 0, msgsize);
			memcpy(snd_strct, system->observables, sizeof(observables_t));
                        memcpy((snd_strct + sizeof(observables_t)), system->avg_nodestats, sizeof(avg_nodestats_t));
			if(system->calc_hist)
				mpi_copy_histogram_to_sendbuffer(snd_strct + sizeof(observables_t) + sizeof(avg_nodestats_t), system->grids->histogram->grid, system);
			if(!rank) memset(rcv_strct, 0, size*msgsize);

#ifdef MPI
			MPI_Gather(snd_strct, 1, msgtype, rcv_strct, 1, msgtype, 0, MPI_COMM_WORLD);
#else
			memcpy(rcv_strct, snd_strct, msgsize);
#endif /* MPI */
			/* head node collects all observables and averages */
			if(!rank) {
				for(j = 0; j < size; j++) {
				
					/* copy from the mpi buffer */
					memcpy(observables_mpi, rcv_strct + j*msgsize, sizeof(observables_t));
					memcpy(avg_nodestats_mpi, rcv_strct + j*msgsize + sizeof(observables_t), sizeof(avg_nodestats_t));
					if(system->calc_hist)
						mpi_copy_rcv_histogram_to_data(rcv_strct + j*msgsize + sizeof(observables_t) + sizeof(avg_nodestats_t), system->grids->histogram->grid, system);

					/* collect the averages */
					update_root_averages(system, observables_mpi, avg_nodestats_mpi, system->avg_observables);
					if(system->calc_hist) update_root_histogram(system);
					if(system->file_pointers.fp_energy) write_observables(system->file_pointers.fp_energy, observables_mpi);

				}
				/* XXX - this needs to be fixed, currently only writing the root node's states */
				if(system->file_pointers.fp_traj) write_states(system->file_pointers.fp_traj, system->molecules);

				/* write the averages to stdout */
				if(system->file_pointers.fp_histogram)
					write_histogram(system->file_pointers.fp_histogram, system->grids->avg_histogram->grid, system);

				if(write_performance(system->step, system) < 0) {
					error("MC: could not write performance data to stdout\n");
					return(-1);
				}
				if(write_averages(system) < 0) {
					error("MC: could not write statistics to stdout\n");
					return(-1);
				}
				if(write_molecules(system, system->pdb_restart) < 0) {
					error("MC: could not write restart state to disk\n");
					return(-1);
				}

				if(system->polarization) {
					write_dipole(system->file_pointers.fp_dipole, system->molecules);
					write_field(system->file_pointers.fp_field, system->molecules);
				}

			} /* !rank */
		} /* corrtime */
	} /* main loop */

	/* write output, close any open files */
	free(snd_strct);
	if(!rank) {

		if(write_molecules(system, system->pdb_output) < 0) {
			error("MC: could not write final state to disk\n");
			return(-1);
		}

		close_files(system);

		free(observables_mpi);
		free(avg_nodestats_mpi);

		free(rcv_strct);

	}

	return(0);

}

