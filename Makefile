# Compila as tres versoes. Uso: make | make seq | make omp | make mpi | make clean
CXX      = g++
MPICXX   = mpicxx
CXXFLAGS = -O2 -Wall

SEQ = sequencial/10_kmeans sequencial/10_kmeans_perfil
OMP = openmp/kmeans_omp
MPI = mpi/kmeans_mpi
ANA = scripts/analise_baseline

.PHONY: all seq omp mpi analise clean

all: seq omp mpi analise

seq: $(SEQ)

sequencial/%: sequencial/%.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# As versoes paralelas so compilam quando o arquivo .cpp existir
omp:
	@if [ -f openmp/kmeans_omp.cpp ]; then $(CXX) $(CXXFLAGS) -fopenmp -o $(OMP) openmp/kmeans_omp.cpp; \
	 else echo "openmp/kmeans_omp.cpp ainda nao existe"; fi

mpi:
	@if [ -f mpi/kmeans_mpi.cpp ]; then $(MPICXX) $(CXXFLAGS) -o $(MPI) mpi/kmeans_mpi.cpp; \
	 else echo "mpi/kmeans_mpi.cpp ainda nao existe"; fi

analise: $(ANA)

$(ANA): scripts/analise_baseline.cpp
	$(CXX) $(CXXFLAGS) -std=c++17 -o $@ $<

clean:
	rm -f $(SEQ) $(OMP) $(MPI) $(ANA)
