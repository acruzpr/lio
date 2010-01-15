#include <iostream>
#include <stdexcept>
#include <cuda_runtime.h>
#include <cstring>
#include "common.h"
#include "matrix.h"
using namespace G2G;
using namespace std;

/***************************
 * Matrix
 ***************************/

template<class T> Matrix<T>::Matrix(void) : data(NULL), width(0), height(0) /*, components(0)*/ {}

template<class T> Matrix<T>::~Matrix(void) { } 

template<class T> unsigned int Matrix<T>::bytes(void) const {
	return elements() * sizeof(T);
}

template<class T> unsigned int Matrix<T>::elements(void) const {
	return width * height /* * components */;
}

template<class T> bool Matrix<T>::is_allocated(void) const {
	return data;
}

/***************************
 * HostMatrix
 ***************************/
template<class T> void HostMatrix<T>::alloc_data(void) {
  assert(this->bytes() != 0);
  
	if (pinned) {
    #if !CPU_KERNELS
		cudaError_t error_status = cudaMallocHost((void**)&this->data, this->bytes());
		assert(error_status != cudaErrorMemoryAllocation);
    #else
    assert(false);
    #endif
	}	
	else this->data = new T[this->elements()];
	
	assert(this->data);
}

template<class T> void HostMatrix<T>::dealloc_data(void) {
	if (pinned) {
    #if !CPU_KERNELS
    cudaFreeHost(this->data);
    #else
    assert(false);
    #endif
  }
	else delete[] this->data;
}

template<class T> void HostMatrix<T>::deallocate(void) {
	dealloc_data();
	this->data = NULL;
  this->width = this->height = 0;
}

template<class T> HostMatrix<T>::HostMatrix(PinnedFlag _pinned) : Matrix<T>() {
  pinned = (_pinned == Pinned);
}

template<class T> HostMatrix<T>::HostMatrix(unsigned int _width, unsigned _height, PinnedFlag _pinned) : Matrix<T>() {
  pinned = (_pinned == Pinned);
  resize(_width, _height);
}

template<class T> HostMatrix<T>::HostMatrix(const CudaMatrix<T>& c) : Matrix<T>(), pinned(false) {
	*this = c;
}

template<class T> HostMatrix<T>::HostMatrix(const HostMatrix<T>& m) : Matrix<T>(), pinned(false) {
	*this = m;
}

template<class T> HostMatrix<T>::~HostMatrix(void) {
	deallocate();
}

template<class T> HostMatrix<T>& HostMatrix<T>::resize(unsigned int _width, unsigned _height) {
  if (_width != this->width || _height != this->height) {
    if (this->data) dealloc_data();
    this->width = _width; this->height = _height;
    alloc_data();
  }
	
	return *this;
}

template<class T> HostMatrix<T>& HostMatrix<T>::fill(const T& value) {
	for (uint i = 0; i < this->elements(); i++) this->data[i] = value;
	return *this;
}

template<class T> HostMatrix<T>& HostMatrix<T>::operator=(const HostMatrix<T>& c) {
	assert(!this->pinned);
	
	if (!c.data) {
		if (this->data) { dealloc_data(); this->width = this->height = 0; this->data = NULL; }
	}
	else {
		if (this->data) {
			if (this->bytes() != c.bytes()) {
				dealloc_data();
				this->width = c.width; this->height = c.height; 
				alloc_data();
			}
		}
		else {
			this->width = c.width; this->height = c.height;
			alloc_data();
		}
		
		copy_submatrix(c);
	}
	
	return *this;
}

template <class T> HostMatrix<T>& HostMatrix<T>::operator=(const CudaMatrix<T>& c) {
	if (!c.data) {
		if (this->data) { dealloc_data(); this->width = this->height = 0; this->data = NULL; }
	}
	else {
		if (this->data) {
			if (this->bytes() != c.bytes()) {
				dealloc_data();
				this->width = c.width; this->height = c.height;
				alloc_data();
			}			
		}
		else {
			this->width = c.width; this->height = c.height;
			alloc_data();
		}

		copy_submatrix(c);
	}

	return *this;		
}

template<class T> void HostMatrix<T>::copy_submatrix(const HostMatrix<T>& c, unsigned int _elements) {
	unsigned int _bytes = (_elements == 0 ? this->bytes() : _elements * sizeof(T));
	//cout << "bytes: " << _bytes << ", c.bytes: " << c.bytes() << endl;
	if (_bytes > c.bytes()) throw runtime_error("Can't copy more elements than what operator has");
	memcpy(this->data, c.data, _bytes);
}

template<class T> void HostMatrix<T>::copy_submatrix(const CudaMatrix<T>& c, unsigned int _elements) {
	unsigned int _bytes = (_elements == 0 ? this->bytes() : _elements * sizeof(T));
	//cout << "bytes: " << _bytes << ", c.bytes: " << c.bytes() << endl;
	if (_bytes > c.bytes()) throw runtime_error("Can't copy more elements than what operator has");

  #if !CPU_KERNELS
	cudaError_t ret = cudaMemcpy(this->data, c.data, _bytes, cudaMemcpyDeviceToHost);
	assert(ret == cudaSuccess);
  #else
  assert(false);
  #endif
}

template<class T> void HostMatrix<T>::to_constant(const char* symbol) {
  #if !CPU_KERNELS
	cudaMemcpyToSymbol(symbol, this->data, this->bytes(), 0, cudaMemcpyHostToDevice);
  #endif
}

template<class T> void HostMatrix<T>::transpose(HostMatrix<T>& out) {
  out.resize(this->height, this->width);
  for (uint i = 0; i < this->width; i++) {
    for (uint j = 0; j < this->height; j++) {
      out.get(j, i) = this->get(i, j);
    }
  }
}

template<class T> void HostMatrix<T>::copy_transpose(const CudaMatrix<T>& cuda_matrix) {
  if (cuda_matrix.width != this->height || cuda_matrix.height != this->width) throw runtime_error("Matrix dimensions for copy_transpose don't agree");
  HostMatrix<T> cuda_matrix_copy(cuda_matrix);
  for (uint i = 0; i < cuda_matrix.width; i++) {
    for (uint j = 0; j < cuda_matrix.height; j++) {
      this->get(j, i) = cuda_matrix_copy.get(i, j);
    }
  }
}

/*template<class T, class S> void HostMatrix<T>::to_constant<S>(const char* constant, const S& value) {
	cudaMemcpyToSymbol(constant, &value, sizeof(S), 0, cudaMemcpyHostToDevice);	
}*/

/******************************
 * CudaMatrix
 ******************************/

template<class T> CudaMatrix<T>::CudaMatrix(void) : Matrix<T>() { }

template<class T> CudaMatrix<T>::CudaMatrix(unsigned int _width, unsigned int _height) : Matrix<T>() {
	resize(_width, _height);
}

template<class T> CudaMatrix<T>& CudaMatrix<T>::resize(unsigned int _width, unsigned int _height) {
  assert(_width * _height != 0);

  #if !CPU_KERNELS
  if (_width != this->width || _height != this->height) {
    if (this->data) cudaFree(this->data);
    this->width = _width; this->height = _height;
    cudaError_t error_status = cudaMalloc((void**)&this->data, this->bytes());
    assert(error_status != cudaErrorMemoryAllocation);
  }
  #endif
	return *this;		
}

template<class T> CudaMatrix<T>& CudaMatrix<T>::fill(int value) {
  #if !CPU_KERNELS
	assert(this->data);
	cudaMemset(this->data, value, this->bytes());
  #endif
	return *this;
}

template<class T> CudaMatrix<T>::CudaMatrix(const CudaMatrix<T>& c) : Matrix<T>() {
	*this = c;
}

template<class T> CudaMatrix<T>::CudaMatrix(const HostMatrix<T>& c) : Matrix<T>() {
	*this = c;
}

template<class T> CudaMatrix<T>::~CudaMatrix(void) {
  deallocate();
}

template<class T> void CudaMatrix<T>::deallocate(void) {
  #if !CPU_KERNELS
	if (this->data) cudaFree(this->data);	
	this->data = NULL;
  #endif
}

template<class T> void CudaMatrix<T>::copy_submatrix(const HostMatrix<T>& c, unsigned int _elements) {
	unsigned int _bytes = (_elements == 0 ? this->bytes() : _elements * sizeof(T));
	//cout << "bytes: " << _bytes << ", c.bytes: " << c.bytes() << endl;
	if (_bytes > c.bytes()) throw runtime_error("CudaMatrix: Can't copy more elements than what operand has");

  #if !CPU_KERNELS
	cudaMemcpy(this->data, c.data, _bytes, cudaMemcpyHostToDevice);
  #endif
}

template<class T> void CudaMatrix<T>::copy_submatrix(const CudaMatrix<T>& c, unsigned int _elements) {
	unsigned int _bytes = (_elements == 0 ? this->bytes() : _elements * sizeof(T));
	if (_bytes > c.bytes()) throw runtime_error("CudaMatrix: Can't copy more elements than what operand has");

  #if !CPU_KERNELS
	cudaMemcpy(c.data, this->data, _bytes, cudaMemcpyDeviceToDevice);
  #endif
}

template<class T> CudaMatrix<T>& CudaMatrix<T>::operator=(const HostMatrix<T>& c) {
  #if !CPU_KERNELS
	if (!c.data) {
		if (this->data) { cudaFree(this->data); this->width = this->height = 0; this->data = NULL; }
	}
	else {
		if (this->data) {
			if (this->bytes() != c.bytes()) {
				cudaFree(this->data);
				this->width = c.width; this->height = c.height;
				cudaError_t error_status = cudaMalloc((void**)&this->data, this->bytes());
				assert(error_status != cudaErrorMemoryAllocation);
			}			
		}
		else {
			this->width = c.width; this->height = c.height;
			cudaError_t error_status = cudaMalloc((void**)&this->data, this->bytes());
			assert(error_status != cudaErrorMemoryAllocation);			
		}
		
		copy_submatrix(c);
	}
  #endif
	return *this;
}

template<class T> CudaMatrix<T>& CudaMatrix<T>::operator=(const CudaMatrix<T>& c) {
  #if !CPU_KERNELS
	// copies data from c, only if necessary (always frees this's data, if any)
	if (!c.data) {
		if (this->data) { cudaFree(this->data); this->width = this->height = 0; this->data = NULL; }
	}
	else {
		if (this->data) {
			if (this->bytes() != c.bytes()) {
				cudaFree(this->data);
				this->width = c.width; this->height = c.height;
				cudaError_t error_status = cudaMalloc((void**)&this->data, this->bytes());
				assert(error_status != cudaErrorMemoryAllocation);				
			}
		}
		else {
			this->width = c.width; this->height = c.height;
			cudaError_t error_status = cudaMalloc((void**)&this->data, this->bytes());
			if (error_status == cudaErrorMemoryAllocation) throw runtime_error("Copy failed! CudaMatrix::operator=");
		}
		
		cudaMemcpy(this->data, c.data, this->bytes(), cudaMemcpyDeviceToDevice);		
	}
	#endif
	return *this;
}

/*************************************
 * FortranMatrix
 *************************************/
template<class T> FortranMatrix<T>::FortranMatrix(void)
	: Matrix<T>(), fortran_width(0)
{ }

template<class T> FortranMatrix<T>::FortranMatrix(T* _data, unsigned int _width, unsigned int _height, unsigned int _fortran_width)
	: Matrix<T>(), fortran_width(_fortran_width)
{
	this->data = _data;
	this->width = _width; this->height = _height;
	assert(this->data);
}

/**
 * Instantiations
 */
template class Matrix<double3>;
template class Matrix<double>;
template class Matrix<float>;
template class Matrix<float1>;
template class Matrix<float2>;
template class Matrix<float3>;
template class Matrix<float4>;
template class Matrix<uint1>;
template class Matrix<uint2>;
template class Matrix<uint>;

template class HostMatrix<double3>;
template class HostMatrix<double>;
template class HostMatrix<float>;
template class HostMatrix<float1>;
template class HostMatrix<float2>;
template class HostMatrix<float3>;
template class HostMatrix<float4>;
template class HostMatrix<uint1>;
template class HostMatrix<uint2>;
template class HostMatrix<uint>;

template class CudaMatrix<float>;
template class CudaMatrix<float1>;
template class CudaMatrix<float2>;
template class CudaMatrix<float3>;
template class CudaMatrix<float4>;
template class CudaMatrix<uint>;
template class CudaMatrix<uint2>;
template class CudaMatrix<double>;

template class FortranMatrix<double>;
template class FortranMatrix<uint>;