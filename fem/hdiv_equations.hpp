#ifndef FILE_HDIV_EQUATIONS
#define FILE_HDIV_EQUATIONS

/*********************************************************************/
/* File:   hdiv_equations.hpp                                        */
/* Author: Joachim Schoeberl, Almedin Becirovic                      */
/* Date:   10. Feb. 2002                                             */
/*********************************************************************/

namespace ngfem
{


/*
  Finite Element Integrators for H(div)

  Mapping with Piola transformation

  Requires H(div) finite elements
*/



/// Identity operator, Piola transformation
template <int D, typename FEL = HDivFiniteElement<D> >
class DiffOpIdHDiv : public DiffOp<DiffOpIdHDiv<D, FEL> >
{
public:
  enum { DIM = 1 };
  enum { DIM_SPACE = D };
  enum { DIM_ELEMENT = D };
  enum { DIM_DMAT = D };
  enum { DIFFORDER = 0 };

  static const FEL & Cast (const FiniteElement & fel) 
  { return static_cast<const FEL&> (fel); }

  /*
  template <typename AFEL, typename MIP, typename MAT>
  static void GenerateMatrix (const AFEL & fel, const MIP & mip,
                              MAT & mat, LocalHeap & lh)
  {
    mat = (1.0/mip.GetJacobiDet()) * 
      (mip.GetJacobian() * Trans (Cast(fel).GetShape(mip.IP(), lh)));
  }
  */

  static void GenerateMatrix (const FiniteElement & fel, 
                              const MappedIntegrationPoint<D,D> & mip,
                              SliceMatrix<double,ColMajor> mat, LocalHeap & lh)
  {
    Cast(fel).CalcMappedShape (mip, Trans(mat));
  }
    
  template <typename MAT>
  static void GenerateMatrix (const FiniteElement & fel, 
                              const MappedIntegrationPoint<D,D,Complex> & mip,
                              MAT && mat, LocalHeap & lh)
  {
    HeapReset hr(lh);

    mat = (1.0/mip.GetJacobiDet()) * 
      (mip.GetJacobian() * Trans (Cast(fel).GetShape(mip.IP(), lh)));
  }

  static void GenerateMatrixSIMDIR (const FiniteElement & fel,
                                    const SIMD_BaseMappedIntegrationRule & mir, BareSliceMatrix<SIMD<double>> mat)
  {
    Cast(fel).CalcMappedShape (mir, mat);      
  }

  
  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void Apply (const AFEL & fel, const MIP & mip,
                     const TVX & x, TVY & y,
                     LocalHeap & lh) 
  {
    HeapReset hr(lh);
    typedef typename TVX::TSCAL TSCAL;
    
    Vec<D,TSCAL> hv = Trans (Cast(fel).GetShape(mip.IP(), lh)) * x;
    hv *= (1.0/mip.GetJacobiDet());
    y = mip.GetJacobian() * hv;
  }

  using DiffOp<DiffOpIdHDiv<D, FEL> >::ApplyIR;

  template <typename AFEL, class MIR>
  static void ApplyIR (const AFEL & fel, const MIR & mir,
		       FlatVector<double> x, FlatMatrixFixWidth<D,double> y,
		       LocalHeap & lh)
  {
    static Timer t("ApplyIR - HDivfe");
    t.Start();
    Cast(fel).Evaluate (mir.IR(), x, y);
    t.Stop();

    for (int i = 0; i < mir.Size(); i++)
      {
	Vec<D> hy = mir[i].GetJacobian() * y.Row(i);
	y.Row(i) = (1.0 / mir[i].GetJacobiDet()) * hy;
      }
  }



  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void ApplyTrans (const AFEL & fel, const MIP & mip,
			  const TVX & x, TVY & y,
			  LocalHeap & lh) 
  {
    HeapReset hr(lh);
    typedef typename TVX::TSCAL TSCAL;

    Vec<D,TSCAL> hv = Trans (mip.GetJacobian()) * x;
    hv *= (1.0/mip.GetJacobiDet());
    y = Cast(fel).GetShape(mip.IP(),lh) * hv;
  }

  using DiffOp<DiffOpIdHDiv<D,FEL>>::ApplySIMDIR;        
  static void ApplySIMDIR (const FiniteElement & fel, const SIMD_BaseMappedIntegrationRule & mir,
                           BareSliceVector<double> x, BareSliceMatrix<SIMD<double>> y)
  {
    Cast(fel).Evaluate (mir, x, y);
  }    

  using DiffOp<DiffOpIdHDiv<D,FEL>>::AddTransSIMDIR;          
  static void AddTransSIMDIR (const FiniteElement & fel, const SIMD_BaseMappedIntegrationRule & mir,
                              BareSliceMatrix<SIMD<double>> y, BareSliceVector<double> x)
  {
    Cast(fel).AddTrans (mir, y, x);
  }    
  
};




/// divergence Operator
template <int D, typename FEL = HDivFiniteElement<D> >
class DiffOpDivHDiv : public DiffOp<DiffOpDivHDiv<D, FEL> >
{
public:
  enum { DIM = 1 };
  enum { DIM_SPACE = D };
  enum { DIM_ELEMENT = D };
  enum { DIM_DMAT = 1 };
  enum { DIFFORDER = 1 };

  static string Name() { return "div"; }

  static const FEL & Cast (const FiniteElement & fel) 
  { return static_cast<const FEL&> (fel); }
  
  template <typename AFEL, typename MIP, typename MAT>
  static void GenerateMatrix (const AFEL & fel, const MIP & mip,
                              MAT & mat, LocalHeap & lh)
  {
    HeapReset hr(lh);
    mat = 1.0/mip.GetJacobiDet() *
      Trans (static_cast<const FEL&>(fel).GetDivShape(mip.IP(), lh));
  }

  template <typename AFEL, typename MIP>
  static void GenerateMatrix (const AFEL & fel, const MIP & mip,
                              FlatVector<double> & mat, LocalHeap & lh)
  {
    HeapReset hr(lh);
    mat = 1.0/mip.GetJacobiDet() * 
      (static_cast<const FEL&>(fel).GetDivShape(mip.IP(), lh));
  }

  static void GenerateMatrixSIMDIR (const FiniteElement & fel,
                                    const SIMD_BaseMappedIntegrationRule & mir, BareSliceMatrix<SIMD<double>> mat)
  {
    Cast(fel).CalcMappedDivShape (mir, mat);      
  }


  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void Apply (const AFEL & fel, const MIP & mip,
                     const TVX & x, TVY & y,
                     LocalHeap & lh) 
  {
    HeapReset hr(lh);
    typedef typename TVX::TSCAL TSCAL;
    Vec<DIM,TSCAL> hv = Trans (static_cast<const FEL&>(fel).GetDivShape(mip.IP(), lh)) * x;
    y = (1.0/mip.GetJacobiDet()) * hv;
  }

  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void ApplyTrans (const AFEL & fel, const MIP & mip,
			  const TVX & x, TVY & y,
			  LocalHeap & lh) 
  {
    HeapReset hr(lh);      
    typedef typename TVX::TSCAL TSCAL;
    Vec<DIM,TSCAL> hv = x;
    hv *= (1.0/mip.GetJacobiDet());
    y = static_cast<const FEL&>(fel).GetDivShape(mip.IP(),lh) * hv;
  }


  using DiffOp<DiffOpDivHDiv<D,FEL>>::ApplySIMDIR;          
  static void ApplySIMDIR (const FiniteElement & fel, const SIMD_BaseMappedIntegrationRule & mir,
                           BareSliceVector<double> x, BareSliceMatrix<SIMD<double>> y)
  {
    Cast(fel).EvaluateDiv (mir, x, y.Row(0));
  }    

  using DiffOp<DiffOpDivHDiv<D,FEL>>::AddTransSIMDIR;            
  static void AddTransSIMDIR (const FiniteElement & fel, const SIMD_BaseMappedIntegrationRule & mir,
                              BareSliceMatrix<SIMD<double>> y, BareSliceVector<double> x)
  {
    Cast(fel).AddDivTrans (mir, y.Row(0), x);
  }    
  
  
};




/// Identity for boundary-normal elements
template <int D, typename FEL = HDivNormalFiniteElement<D-1> >
class DiffOpIdHDivBoundary : public DiffOp<DiffOpIdHDivBoundary<D, FEL> >
{
public:
  enum { DIM = 1 };
  enum { DIM_SPACE = D };
  enum { DIM_ELEMENT = D-1 };
  enum { DIM_DMAT = 1 };
  enum { DIFFORDER = 0 };

  template <typename AFEL, typename MIP, typename MAT>
  static void GenerateMatrix (const AFEL & fel, const MIP & mip,
			      MAT & mat, LocalHeap & lh)
  {
    mat =  (1.0/mip.GetJacobiDet())*
      Trans(static_cast<const FEL&> (fel).GetShape (mip.IP(), lh));
  }

  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void Apply (const AFEL & fel, const MIP & mip,
		     const TVX & x, TVY && y,
		     LocalHeap & lh)
  {
    y = (1.0/mip.GetJacobiDet())*
      (Trans (static_cast<const FEL&> (fel).GetShape (mip.IP(), lh)) * x);
  }

  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void ApplyTrans (const AFEL & fel, const MIP & mip,
			  const TVX & x, TVY & y,
			  LocalHeap & lh)
  {
    y = static_cast<const FEL&> (fel).GetShape (mip.IP(), lh)*((1.0/mip.GetJacobiDet())* x);
  }
};


/// Identity for boundary-normal elements, gives q_n n
template <int D, typename FEL = HDivNormalFiniteElement<D-1> >
class DiffOpIdVecHDivBoundary : public DiffOp<DiffOpIdVecHDivBoundary<D,FEL> >
{
public:
  enum { DIM = 1 };
  enum { DIM_SPACE = D };
  enum { DIM_ELEMENT = D-1 };
  enum { DIM_DMAT = D };
  enum { DIFFORDER = 0 };

  static const FEL & Cast (const FiniteElement & fel) 
  { return static_cast<const FEL&> (fel); }

  template <typename AFEL, typename MIP, typename MAT>
  static void GenerateMatrix (const AFEL & fel, const MIP & mip,
			      MAT & mat, LocalHeap & lh)
  {
    // Vec<D> scaled_nv = (1.0/mip.GetJacobiDet()) * mip.GetNV();
    auto scaled_nv = (1.0/mip.GetJacobiDet()) * mip.GetNV();
    mat = scaled_nv * Trans(Cast(fel).GetShape (mip.IP(), lh));
    /*
    mat =  (1.0/mip.GetJacobiDet())*
      Trans(static_cast<const FEL&> (fel).GetShape (mip.IP(), lh)) 
      * Trans (mip.GetNV());
      */
  }

  template <typename AFEL, typename MIP, class TVX, class TVY> 
  static void Apply (const AFEL & fel, const MIP & mip,
		     const TVX & x, TVY & y,
		     LocalHeap & lh)
  {
    y = ( (1.0/mip.GetJacobiDet())*(InnerProduct (Cast(fel).GetShape (mip.IP(), lh), x) )) * mip.GetNV();
  }

  template <typename AFEL, typename MIP, class TVX, class TVY>
  static void ApplyTrans (const AFEL & fel, const MIP & mip,
			  const TVX & x, TVY & y,
			  LocalHeap & lh)
  {
    y = ((1.0/mip.GetJacobiDet())* InnerProduct (x, mip.GetNV()) ) * Cast(fel).GetShape (mip.IP(), lh);
  }
};




/// Integrator for term of zero-th order
template <int D>
class MassHDivIntegrator
  : public T_BDBIntegrator<DiffOpIdHDiv<D>, DiagDMat<D>, HDivFiniteElement<D> >
{
  typedef  T_BDBIntegrator<DiffOpIdHDiv<D>, DiagDMat<D>, HDivFiniteElement<D> > BASE;
public:
  using T_BDBIntegrator<DiffOpIdHDiv<D>, DiagDMat<D>, HDivFiniteElement<D> >::T_BDBIntegrator;
  virtual string Name () const { return "MassHDiv"; }
};

  

/// Integrator for div u div v
template <int D>
class DivDivHDivIntegrator
  : public T_BDBIntegrator<DiffOpDivHDiv<D>, DiagDMat<1>, HDivFiniteElement<D> >
{
  typedef  T_BDBIntegrator<DiffOpDivHDiv<D>, DiagDMat<1>, HDivFiniteElement<D> > BASE;
public:
  using T_BDBIntegrator<DiffOpDivHDiv<D>, DiagDMat<1>, HDivFiniteElement<D> >::T_BDBIntegrator;
  virtual string Name () const { return "DivDivHDiv"; }
};




/// source term integrator for \ff div v
template <int D>
class NGS_DLL_HEADER DivSourceHDivIntegrator 
  : public T_BIntegrator<DiffOpDivHDiv<D>, DVec<1>, HDivFiniteElement<D> >
{
  typedef  T_BIntegrator<DiffOpDivHDiv<D>, DVec<1>, HDivFiniteElement<D> > BASE;
public:
  using T_BIntegrator<DiffOpDivHDiv<D>, DVec<1>, HDivFiniteElement<D> >::T_BIntegrator;
  virtual string Name () const { return "DivSourceHDiv"; }
};

/*
/// source term for H(div)
template <int D>
class SourceHDivIntegrator 
  : public T_BIntegrator<DiffOpIdHDiv<D>, DVec<D>, HDivFiniteElement<D> >
{
public:
  SourceHDivIntegrator (CoefficientFunction * coeff1,
                        CoefficientFunction * coeff2,
                        CoefficientFunction * coeff3);

  SourceHDivIntegrator (CoefficientFunction * coeff1,
                        CoefficientFunction * coeff2);

  static Integrator * Create (Array<CoefficientFunction*> & coeffs)
  {
    if(D == 2)
      return new SourceHDivIntegrator (coeffs[0], coeffs[1]);
    else if (D == 3)
      return new SourceHDivIntegrator (coeffs[0], coeffs[1], coeffs[2]);
  }
    
  ///
  virtual string Name () const { return "SourceHDiv"; }
  };
*/


  template <int D> class SourceHDivIntegrator;


template <int D>
class NGS_DLL_HEADER BaseSourceHDivIntegrator 
  : public T_BIntegrator<DiffOpIdHDiv<D>, DVec<D>, HDivFiniteElement<D> >
{
  typedef  T_BIntegrator<DiffOpIdHDiv<D>, DVec<D>, HDivFiniteElement<D> > BASE;
public:
  using T_BIntegrator<DiffOpIdHDiv<D>, DVec<D>, HDivFiniteElement<D> >::T_BIntegrator;
  virtual string Name () const { return "SourceHDiv"; }
};



template <>
class NGS_DLL_HEADER SourceHDivIntegrator<2>
  : public BaseSourceHDivIntegrator<2> 
{
  typedef  BaseSourceHDivIntegrator<2>  BASE;
public:
  using BaseSourceHDivIntegrator<2>::BaseSourceHDivIntegrator;
  SourceHDivIntegrator() = delete;
};

template <>
class NGS_DLL_HEADER SourceHDivIntegrator<3>
  : public BaseSourceHDivIntegrator<3> 
{
  typedef  BaseSourceHDivIntegrator<3>  BASE;
public:
  using BaseSourceHDivIntegrator<3>::BaseSourceHDivIntegrator;
  SourceHDivIntegrator() = delete;
};



template <int D>
class NGS_DLL_HEADER SourceHDivIntegratorN 
  : public T_BIntegrator<DiffOpIdHDiv<D>, DVecN<D>, HDivFiniteElement<D> >
{
  typedef  T_BIntegrator<DiffOpIdHDiv<D>, DVecN<D>, HDivFiniteElement<D> > BASE;
public:
  using T_BIntegrator<DiffOpIdHDiv<D>, DVecN<D>, HDivFiniteElement<D> >::T_BIntegrator;
  virtual string Name () const { return "VecSourceHDiv"; }
};







///
template <int D, typename FEL = HDivNormalFiniteElement<D-1> >
class NGS_DLL_HEADER NeumannHDivIntegrator
  : public T_BIntegrator<DiffOpIdHDivBoundary<D>, DVec<1>, FEL>
{
  typedef  T_BIntegrator<DiffOpIdHDivBoundary<D>, DVec<1>, FEL> BASE;
public:
  using T_BIntegrator<DiffOpIdHDivBoundary<D>, DVec<1>, FEL>::T_BIntegrator;
  ///
  /*
  NeumannHDivIntegrator (CoefficientFunction * coeff)
    : T_BIntegrator<DiffOpIdHDivBoundary<D>, DVec<1>, FEL> (DVec<1> (coeff))
  { ; }

  static Integrator * Create (Array<CoefficientFunction*> & coeffs)
  {
    return new NeumannHDivIntegrator (coeffs[0]);
  }
  */
  ///
  virtual bool BoundaryForm () const { return 1; }
  ///
  virtual string Name () const { return "NeumannHDiv"; }
};


/// integrator for \f$\int_\Gamma \sigma_n \tau_n \, ds\f$
template <int D>
class RobinHDivIntegrator
  : public T_BDBIntegrator<DiffOpIdVecHDivBoundary<D>, DiagDMat<D>, HDivNormalFiniteElement<D-1> >
{
  typedef  T_BDBIntegrator<DiffOpIdVecHDivBoundary<D>, DiagDMat<D>, HDivNormalFiniteElement<D-1> > BASE;
public:
  using T_BDBIntegrator<DiffOpIdVecHDivBoundary<D>, DiagDMat<D>, HDivNormalFiniteElement<D-1> >::T_BDBIntegrator;
  /*
  NGS_DLL_HEADER RobinHDivIntegrator (CoefficientFunction * coeff)
    : T_BDBIntegrator<DiffOpIdVecHDivBoundary<D>, DiagDMat<D>, HDivNormalFiniteElement<D-1> > (DiagDMat<D> (coeff))
  { ; }


  static Integrator * Create (Array<CoefficientFunction*> & coeffs)
  {
    return new RobinHDivIntegrator (coeffs[0]);

  }
  */
  virtual bool BoundaryForm () const { return 1; }
  virtual string Name () const { return "RobinHDiv"; }
};


}

  
#ifdef FILE_HDIV_EQUATIONS_CPP
#define HDIV_EQUATIONS_EXTERN
#else
#define HDIV_EQUATIONS_EXTERN extern
#endif
  
namespace ngfem
{

HDIV_EQUATIONS_EXTERN template class NGS_DLL_HEADER T_DifferentialOperator<DiffOpIdHDiv<2> >;
HDIV_EQUATIONS_EXTERN template class NGS_DLL_HEADER T_DifferentialOperator<DiffOpIdHDiv<3> >;

HDIV_EQUATIONS_EXTERN template class MassHDivIntegrator<2>;
HDIV_EQUATIONS_EXTERN template class DivDivHDivIntegrator<2>;
  // HDIV_EQUATIONS_EXTERN template class SourceHDivIntegrator<2>;
HDIV_EQUATIONS_EXTERN template class SourceHDivIntegratorN<2>;
HDIV_EQUATIONS_EXTERN template class DivSourceHDivIntegrator<2>;

HDIV_EQUATIONS_EXTERN template class MassHDivIntegrator<3>;
HDIV_EQUATIONS_EXTERN template class DivDivHDivIntegrator<3>;
// HDIV_EQUATIONS_EXTERN template class SourceHDivIntegrator<3>;
HDIV_EQUATIONS_EXTERN template class SourceHDivIntegratorN<3>;
HDIV_EQUATIONS_EXTERN template class DivSourceHDivIntegrator<3>;



}


#endif



