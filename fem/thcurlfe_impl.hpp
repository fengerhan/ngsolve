#ifndef FILE_THCURLFE_IMPL
#define FILE_THCURLFE_IMPL



namespace ngfem
{

  template <int DIM>
  class HCurl_Shape : public Vec<DIM>
  {
  public:
    template <typename T>
    HCurl_Shape (T shape) : Vec<DIM>(shape.Value()) { ; }
  };

  template <int DIM>
  class HCurl_CurlShape : public Vec<DIM_CURL_TRAIT<DIM>::DIM>
  {
  public:
    template <typename T>
    HCurl_CurlShape (T shape) 
      : Vec<DIM_CURL_TRAIT<DIM>::DIM> (shape.CurlValue()) { ; }
  };



  
  /*******************************************/
  /* T_HCurlHOFiniteElement                  */
  /*******************************************/

  
  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  void T_HCurlHighOrderFiniteElement<ET,SHAPES,BASE> :: 
  CalcShape (const IntegrationPoint & ip, SliceMatrix<> shape) const
  {    
    Vec<DIM, AutoDiff<DIM> > adp = ip; 
    T_CalcShape (&adp(0), SBLambda ([&](int i, HCurl_Shape<DIM> s) 
                                    { shape.Row(i) = s; }));
  }
  
  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  void T_HCurlHighOrderFiniteElement<ET, SHAPES,BASE> :: 
  CalcCurlShape (const IntegrationPoint & ip, SliceMatrix<> shape) const
  {  
    Vec<DIM, AutoDiff<DIM> > adp = ip; 
    T_CalcShape (&adp(0), SBLambda ([&](int i, HCurl_CurlShape<DIM> s) 
                                    { shape.Row(i) = s; }));
  } 

  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  void T_HCurlHighOrderFiniteElement<ET, SHAPES, BASE> :: 
  CalcMappedShape (const MappedIntegrationPoint<DIM,DIM> & mip,
                   SliceMatrix<> shape) const
  {
    Vec<DIM, AutoDiff<DIM> > adp = mip; 
    T_CalcShape (&adp(0), SBLambda ([&](int i, HCurl_Shape<DIM> s) 
                                { shape.Row(i) = s; }));
  }

  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  void T_HCurlHighOrderFiniteElement<ET,SHAPES,BASE> :: 
  CalcMappedCurlShape (const MappedIntegrationPoint<DIM,DIM> & mip,
                       SliceMatrix<> curlshape) const
  { 
    if (DIM == 2)
      {
        CalcCurlShape (mip.IP(), curlshape);
        curlshape /= mip.GetJacobiDet();        
      }
    else
      {
        Vec<DIM, AutoDiff<DIM> > adp = mip; 
        T_CalcShape (&adp(0), SBLambda ([&](int i, HCurl_CurlShape<DIM> s) 
                                        { curlshape.Row(i) = s; }));
      }
  }

  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  auto T_HCurlHighOrderFiniteElement<ET,SHAPES,BASE> :: 
  EvaluateCurlShape (const IntegrationPoint & ip, 
                     FlatVector<double> x,
                     LocalHeap & lh) const -> Vec<DIM_CURL_(DIM)>
  {
    Vec<DIM, AutoDiff<DIM> > adp = ip; 
    Vec<DIM_CURL_(DIM)> sum = 0.0;
    T_CalcShape (&adp(0), SBLambda ([&](int i, HCurl_CurlShape<DIM> s) 
                                    { sum += x(i) * s; }));
    return sum;
  }

  template <ELEMENT_TYPE ET, typename SHAPES, typename BASE>
  void T_HCurlHighOrderFiniteElement<ET,SHAPES,BASE> :: 
  EvaluateCurl (const IntegrationRule & ir, FlatVector<> coefs, FlatMatrixFixWidth<DIM_CURL_(DIM)> curl) const
  {
    LocalHeapMem<1000> lhdummy("dummy");
    for (int i = 0; i < ir.Size(); i++)
      curl.Row(i) = EvaluateCurlShape (ir[i], coefs, lhdummy);
  }


}


#endif
