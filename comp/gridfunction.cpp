#include <comp.hpp>
#include <multigrid.hpp>

#include <parallelngs.hpp>
#include <stdlib.h>

using namespace ngcomp; 


#ifdef PARALLEL
#include "../parallel/dump.hpp"

template <NODE_TYPE NT, typename TELEM>
class NodalArray
{
  const MeshAccess & ma;
  Array<TELEM> & a;
public:
  NodalArray (const MeshAccess & ama, Array<TELEM> & aa) : ma(ama), a(aa) { ; }
  const MeshAccess & GetMeshAccess() const { return ma; }
  Array<TELEM> & A() { return a; }
};

template <NODE_TYPE NT, typename TELEM>
auto NodalData (const MeshAccess & ama, Array<TELEM> & a) -> NodalArray<NT,TELEM> 
{ return NodalArray<NT,TELEM> (ama, a); }



template <NODE_TYPE NT, typename T> 
Archive & operator & (Archive & archive, NodalArray<NT,T> && a)
{
  if (MyMPI_GetNTasks() == 1) return archive & a.A();
  
  auto g = [&] (int size) { archive & size; };    

  typedef typename key_trait<NT>::TKEY TKEY;
  auto f = [&] (TKEY key, T val) { archive & val; };
      
  GatherNodalData<NT> (a.GetMeshAccess(), a.A(), g, f);

  return archive;
}



#else
template <NODE_TYPE NT, typename TELEM>
auto NodalData (MeshAccess & ma, Array<TELEM> & a) -> Array<TELEM> & { return a; }
#endif




namespace ngcomp
{
  using namespace ngmg;
  
  GridFunction :: GridFunction (shared_ptr<FESpace> afespace, const string & name,
				const Flags & flags)
    : NGS_Object (afespace->GetMeshAccess(), flags, name), 
      // GridFunctionCoefficientFunction (shared_ptr<GridFunction>(this, NOOP_Deleter), afespace->GetEvaluator()),
      /*
      GridFunctionCoefficientFunction (shared_ptr<DifferentialOperator>(),
                                       shared_ptr<DifferentialOperator>()), // gridfunction-CF with null-ptr diffop
      */
      GridFunctionCoefficientFunction (afespace->GetEvaluator(VOL), afespace->GetEvaluator(BND),
                                       afespace->GetEvaluator(BBND)),
      fespace(afespace)
  {
    GridFunctionCoefficientFunction::gf = shared_ptr<GridFunction>(this,NOOP_Deleter);
    GridFunctionCoefficientFunction::fes = fespace;

    is_complex = fespace->IsComplex();
    if (fespace->GetEvaluator(VOL) || fespace->GetEvaluator(BND))
      SetDimensions (GridFunctionCoefficientFunction::Dimensions());
    nested = flags.GetDefineFlag ("nested");
    visual = !flags.GetDefineFlag ("novisual");
    multidim = int (flags.GetNumFlag ("multidim", 1));
    // level_updated = -1;
    // cacheblocksize = 1;
  }


  GridFunction :: ~GridFunction() { ; }


  void GridFunction :: Update ()
  {
    throw Exception("GridFunction::Update not overloaded");
  }

  void GridFunction :: DoArchive (Archive & archive)
  {
    archive & nested & visual & multidim & level_updated;
    archive & cacheblocksize;

    if (archive.Input()) Update();
    for (int i = 0; i < vec.Size(); i++)
      {
        FlatVector<double> fv = vec[i] -> FVDouble();

	/*
        for (int i = 0; i < fv.Size(); i++)
          archive & fv(i);
	*/

	if (archive.Output())
	  {
	    Array<DofId> dnums;
	    Array<double> temp (ma->GetNV());
	    temp = 0;
	    for (int i = 0; i < ma->GetNV(); i++)
	      {
		fespace->GetDofNrs (NodeId(NT_VERTEX, i), dnums);
		if (dnums.Size())
		  temp[i] = fv(dnums[0]);
	      }
	    archive & NodalData<NT_VERTEX> (*ma, temp);
	  }
	else
	  {
	    Array<double> temp;
	    archive & NodalData<NT_VERTEX> (*ma, temp);

	    Array<DofId> dnums;
	    for (int i = 0; i < ma->GetNV(); i++)
	      {
		fespace->GetDofNrs (NodeId(NT_VERTEX, i), dnums);
		if (dnums.Size())
		  fv(dnums[0]) = temp[i];
	      }

	  }

	if (archive.Output())
	  {
	    Array<DofId> dnums;
	    Array<double> temp (ma->GetNEdges());
	    temp = 0;
	    for (int i = 0; i < ma->GetNEdges(); i++)
	      {
		fespace->GetDofNrs (NodeId(NT_EDGE, i), dnums);
		if (dnums.Size())
		  temp[i] = fv(dnums[0]);
	      }
	    archive & NodalData<NT_EDGE> (*ma, temp);
	  }
	else
	  {
	    Array<double> temp;
	    archive & NodalData<NT_EDGE> (*ma, temp);

	    Array<DofId> dnums;
	    for (int i = 0; i < ma->GetNEdges(); i++)
	      {
		fespace->GetDofNrs (NodeId(NT_EDGE, i), dnums);
		if (dnums.Size())
		  fv(dnums[0]) = temp[i];
	      }

	  }



      }
    if (archive.Input())
      Visualize(shared_ptr<GridFunction>(this, NOOP_Deleter), name);
  }


  bool GridFunction :: IsUpdated () const
  {
    int ndof = fespace->GetNDof();
    for (int i = 0; i < multidim; i++)
      {
	if (!vec[i]) return false;
	if (ndof != vec[i]->Size()) return false;
      }
    return true;
  }

  void GridFunction :: PrintReport (ostream & ost) const
  {
    ost << "gridfunction '" << GetName() << "' on space '" 
        << fespace->GetName() << "'\n"
	<< "nested = " << nested << endl;
  }

  void GridFunction :: MemoryUsage (Array<MemoryUsageStruct*> & mu) const
  {
    //if (&const_cast<GridFunction&> (*this).GetVector())
    if (this->GetVectorPtr())
      {
	int olds = mu.Size();
	//const_cast<GridFunction&> (*this).GetVector().MemoryUsage (mu);
	this->GetVector().MemoryUsage (mu);
	for (int i = olds; i < mu.Size(); i++)
	  mu[i]->AddName (string(" gf ")+GetName());
      }
  }



  void GridFunction :: AddMultiDimComponent (BaseVector & v)
  {
    vec.SetSize (vec.Size()+1);
    vec[multidim] = v.CreateVector();
    *vec[multidim] = v;
    multidim++;
  }


  // void GridFunction :: Visualize(const string & given_name)
  void Visualize(shared_ptr<GridFunction> gf, const string & given_name)
  {
    // if (!visual) return;

    /*
      for (int i = 0; i < compgfs.Size(); i++)
      {
      stringstream child_name;
      child_name << given_name << "_" << i+1;
      compgfs[i] -> Visualize (child_name.str());
      }
    */

    shared_ptr<BilinearFormIntegrator> bfi2d, bfi3d;

    auto fespace = gf->GetFESpace();
    auto ma = fespace->GetMeshAccess();
    
    if (ma->GetDimension() == 2)
      {
	bfi2d = fespace->GetIntegrator(VOL);
      }
    else
      {
	bfi3d = fespace->GetIntegrator(VOL);
	bfi2d = fespace->GetIntegrator(BND);
      }

    if (bfi2d || bfi3d)
      {
        netgen::SolutionData * vis;
	if (!fespace->IsComplex())
	  vis = new VisualizeGridFunction<double> (ma, gf, bfi2d, bfi3d, 0);
	else
	  vis = new VisualizeGridFunction<Complex> (ma, gf, bfi2d, bfi3d, 0);

	Ng_SolutionData soldata;
	Ng_InitSolutionData (&soldata);
	
	soldata.name = given_name.c_str();
	soldata.data = 0;
	soldata.components = vis->GetComponents();
	soldata.iscomplex = vis->IsComplex();
	soldata.draw_surface = bfi2d != 0;
	soldata.draw_volume  = bfi3d != 0;
	soldata.dist = soldata.components;
	soldata.soltype = NG_SOLUTION_VIRTUAL_FUNCTION;
	soldata.solclass = vis;
	Ng_SetSolutionData (&soldata);    
      }
  }



  shared_ptr<GridFunction> GridFunction :: 
  GetComponent (int compound_comp) const
  {
    if (!compgfs.Size() || compgfs.Size() < compound_comp)
      throw Exception("GetComponent: compound_comp does not exist!");
    else
      return compgfs[compound_comp];
  }




  template <int N>
  bool MyLess (const Vec<N,int>& a, const Vec<N,int>& b)
  {
    for( int i = 0; i < N; i++)
      {
	if (a[i] < b[i]) return true;
	if (a[i] > b[i]) return false;
      }
    
    return false;  
  }






  
  template <class SCAL>
  void S_GridFunction<SCAL> :: Load (istream & ist)
  {
    if (MyMPI_GetNTasks() == 1)
      { 
	const FESpace & fes = *GetFESpace();
	Array<DofId> dnums;
	
	// for (NODE_TYPE nt = NT_VERTEX; nt <= NT_CELL; nt++)
        for (NODE_TYPE nt : { NT_VERTEX, NT_EDGE, NT_FACE, NT_CELL })
	  {
	    int nnodes = ma->GetNNodes (nt);

	    
	    Array<Vec<8, int> > nodekeys;
	    Array<int> pnums, compress;
	    for(int i = 0; i < nnodes; i++)
	      {
		fes.GetDofNrs (NodeId(nt, i),  dnums);
		if (dnums.Size() == 0) continue;
		
		switch (nt)
		  {
		  case NT_VERTEX: pnums.SetSize(1); pnums[0] = i; break;
		  case NT_EDGE: ma->GetEdgePNums (i, pnums); break;
		  case NT_FACE: ma->GetFacePNums (i, pnums); break;
		  case NT_CELL: ma->GetElPNums (i, pnums); break;
                  default:
                    __assume(false);
		  }
		Vec<8> key; 
		key = -1;
		for (int j = 0; j < pnums.Size(); j++)
		  key[j] = pnums[j];
		nodekeys.Append (key);
		compress.Append (i);
	      }
	    
	    nnodes = nodekeys.Size();

	    Array<int> index(nnodes);
	    for( int i = 0; i < index.Size(); i++) index[i] = i;
	    
	    QuickSortI (nodekeys, index, MyLess<8>);
	    
	    for( int i = 0; i < nnodes; i++)
	      {
		fes.GetDofNrs (NodeId(nt, compress[index[i]]),  dnums); 
		Vector<SCAL> elvec(dnums.Size()*fes.GetDimension());
		
		for (int k = 0; k < elvec.Size(); k++)
		  if (ist.good())
		    LoadBin<SCAL>(ist, elvec(k));			
	  
		SetElementVector (dnums, elvec);
	      }
	  }
      }
#ifdef PARALLEL	    
    else
      {  
	GetVector() = 0.0;
	GetVector().SetParallelStatus (DISTRIBUTED);    
	LoadNodeType<1,NT_VERTEX> (ist);
	LoadNodeType<2,NT_EDGE> (ist);
	LoadNodeType<4,NT_FACE> (ist);
	LoadNodeType<8,NT_CELL> (ist);
	GetVector().Cumulate();
      }
#endif
  }


  template <class SCAL>  template <int N, NODE_TYPE NTYPE>
  void  S_GridFunction<SCAL> :: LoadNodeType (istream & ist) 
  {
#ifdef PARALLEL
    int id = MyMPI_GetId();
    int ntasks = MyMPI_GetNTasks();
    
    const FESpace & fes = *GetFESpace();
    ParallelDofs & par = fes.GetParallelDofs ();
    
    if(id > 0)
      {
	int nnodes = ma->GetNNodes (NTYPE);
	
	Array<Vec<N+1, int> > nodekeys;
	Array<int> master_nodes;
	Array<DofId> dnums, pnums;
	
	for(int i = 0; i < nnodes; i++)
	  {
	    fes.GetNodeDofNrs (NTYPE, i,  dnums);
	    
	    if (dnums.Size() == 0) continue;
	    if (!par.IsMasterDof (dnums[0])) continue;
	    
	    master_nodes.Append(i);

	    switch (NTYPE)
	      {
	      case NT_VERTEX: pnums.SetSize(1); pnums[0] = i; break;
	      case NT_EDGE: ma->GetEdgePNums (i, pnums); break;
	      case NT_FACE: ma->GetFacePNums (i, pnums); break;
	      case NT_CELL: ma->GetElPNums (i, pnums); break;
	      }

	    Vec<N+1, int> key;
	    key = -1;
	    for (int j = 0; j < pnums.Size(); j++)
	      key[j] = ma->GetGlobalNodeNum (Node(NT_VERTEX, pnums[j]));
	    key[N] = dnums.Size();
	    
	    nodekeys.Append (key);	
	  }
	
	MyMPI_Send (nodekeys, 0, 12);
	
	Array<SCAL> loc_data;
	MyMPI_Recv (loc_data, 0, 13);
	
	for (int i = 0, cnt = 0; i < master_nodes.Size(); i++)
	  {
	    fes.GetNodeDofNrs (NTYPE, master_nodes[i], dnums); 
	    Vector<SCAL> elvec(dnums.Size());
	    
	    for (int j = 0; j < elvec.Size(); j++)
	      elvec(j) = loc_data[cnt++];
	    
	    SetElementVector (dnums, elvec);
	  }
      }
    else
      {
	Array<Vec<N, int> > nodekeys;
	Array<int> nrdofs_per_node;
	
	Array<Array<int>* > nodenums_of_procs (ntasks-1);
	
	int actual=0;
	for( int proc = 1; proc < ntasks; proc++)
	  {
	    Array<Vec<N+1, int> > nodenums_proc;
	    MyMPI_Recv(nodenums_proc,proc,12);
	    nodenums_of_procs[proc-1] = new Array<int> (nodenums_proc.Size());
	    
	    for (int j=0; j < nodenums_proc.Size(); j++)
	      {
		Vec<N, int>  key;
		for (int k = 0; k < N; k++)
		  key[k] = nodenums_proc[j][k];
		
		nodekeys.Append (key);
		
		nrdofs_per_node.Append (nodenums_proc[j][N]);
		
		(*nodenums_of_procs[proc-1])[j]=actual++;
	      }
	  }
	
	int nnodes = nodekeys.Size();

	Array<int> index(nnodes);
	for( int i = 0; i < index.Size(); i++) index[i] = i;

	QuickSortI (nodekeys, index, MyLess<N>);

	Array<int> inverse_index(nnodes);
	for (int i = 0; i < index.Size(); i++ ) 	
	  inverse_index[index[i]] = i;
	
	int ndofs = 0;
	Array<int> first_node_dof (nnodes+1);
	
	for (int i = 0; i < nnodes; i++)
	  {
	    first_node_dof[i] = ndofs;
	    ndofs += nrdofs_per_node[index[i]];
	  }
	first_node_dof[nnodes] = ndofs;
	
	Array<SCAL> node_data(ndofs);     

	for (int i = 0; i < ndofs; i++)
	  if (ist.good())
	    LoadBin<SCAL> (ist, node_data[i]);
	
	for (int proc = 1; proc < ntasks; proc++)
	  {
	    Array<SCAL> loc_data (0);
	    Array<int> & nodenums_proc = *nodenums_of_procs[proc-1];

	    for (int i = 0; i < nodenums_proc.Size(); i++)
	      {
		int node = inverse_index[nodenums_proc[i]];
		loc_data.Append (node_data.Range (first_node_dof[node], first_node_dof[node+1]));
	      }
	    MyMPI_Send(loc_data,proc,13); 		
	  }
      }
#endif	
  }
  


  template <class SCAL>
  void S_GridFunction<SCAL> :: Save (ostream & ost) const
  {
    int ntasks = MyMPI_GetNTasks();
    const FESpace & fes = *GetFESpace();
  
    if (ntasks == 1)
      {
	Array<DofId> dnums;	    
	// for (NODE_TYPE nt = NT_VERTEX; nt <= NT_CELL; nt++)
        for (NODE_TYPE nt : { NT_VERTEX, NT_EDGE, NT_FACE, NT_CELL })
	  {
	    int nnodes = ma->GetNNodes (nt);

	    Array<Vec<8, int> > nodekeys;
	    Array<int> pnums, compress;
	    for(int i = 0; i < nnodes; i++)
	      {
		fes.GetDofNrs (NodeId(nt, i),  dnums);
		if (dnums.Size() == 0) continue;
		
		switch (nt)
		  {
		  case NT_VERTEX: pnums.SetSize(1); pnums[0] = i; break;
		  case NT_EDGE: ma->GetEdgePNums (i, pnums); break;
		  case NT_FACE: ma->GetFacePNums (i, pnums); break;
		  case NT_CELL: ma->GetElPNums (i, pnums); break;
                  default:
                    __assume(false);
                    break;
		  }
		Vec<8> key; 
		key = -1;
		for (int j = 0; j < pnums.Size(); j++)
		  key[j] = pnums[j];
		nodekeys.Append (key);
		compress.Append (i);
	      }
	    
	    nnodes = nodekeys.Size();

	    Array<int> index(nnodes);
	    for( int i = 0; i < index.Size(); i++) index[i] = i;
	    
	    QuickSortI (nodekeys, index, MyLess<8>);


	    for( int i = 0; i < nnodes; i++)
	      {
		fes.GetDofNrs (NodeId(nt, compress[index[i]]),  dnums); 
		Vector<SCAL> elvec(dnums.Size()*fes.GetDimension());
		GetElementVector (dnums, elvec);
		
		for (int j = 0; j < elvec.Size(); j++)
		  SaveBin<SCAL>(ost, elvec(j));			
	      }
	  }
      }
#ifdef PARALLEL	 
    else
      {  
	GetVector().Cumulate();        
	SaveNodeType<1,NT_VERTEX>(ost);
	SaveNodeType<2,NT_EDGE>  (ost);
	SaveNodeType<4,NT_FACE>  (ost);
	SaveNodeType<8,NT_CELL>  (ost);
      }
#endif
  }





#ifdef PARALLEL
  template <typename T>
  inline void MyMPI_Gather (T d, MPI_Comm comm = ngs_comm)
  {
    static Timer t("dummy - gather"); RegionTimer r(t);
    
    MPI_Gather (&d, 1, MyGetMPIType<T>(), 
		NULL, 1, MyGetMPIType<T>(), 0, comm);
  }

  template <typename T>
  inline void MyMPI_GatherRoot (FlatArray<T> d, MPI_Comm comm = ngs_comm)
  {
    static Timer t("dummy - gather"); RegionTimer r(t);

    d[0] = T(0);
    MPI_Gather (MPI_IN_PLACE, 1, MyGetMPIType<T>(), 
		&d[0], 1, MyGetMPIType<T>(), 0,
		comm);
  }
#endif



  template <class SCAL>  template <int N, NODE_TYPE NTYPE>
  void  S_GridFunction<SCAL> :: SaveNodeType (ostream & ost) const
  {
#ifdef PARALLEL
    int id = MyMPI_GetId();
    int ntasks = MyMPI_GetNTasks();
    
    const FESpace & fes = *GetFESpace();
    ParallelDofs & par = fes.GetParallelDofs ();
    
    if(id > 0)
      { 
	int nnodes = ma->GetNNodes (NTYPE);
	
	Array<Vec<N+1,int> > nodenums;
	Array<SCAL> data;
    
	Array<DofId> dnums;
        Array<int> pnums;
    
	for (int i = 0; i < nnodes; i++)
	  {
	    fes.GetNodeDofNrs (NTYPE, i,  dnums);
	    
	    if (dnums.Size() == 0) continue;
	    if (!par.IsMasterDof (dnums[0])) continue;      

	    switch (NTYPE)
	      {
	      case NT_VERTEX: pnums.SetSize(1); pnums[0] = i; break;
	      case NT_EDGE: ma->GetEdgePNums (i, pnums); break;
	      case NT_FACE: ma->GetFacePNums (i, pnums); break;
	      case NT_CELL: ma->GetElPNums (i, pnums); break;
	      }

	    Vec<N+1, int> points;
	    points = -1;
	    for( int j = 0; j < pnums.Size(); j++)
	      points[j] = ma->GetGlobalNodeNum (Node(NT_VERTEX, pnums[j]));
	    points[N] = dnums.Size();
	    
	    nodenums.Append(points);
	    
	    Vector<SCAL> elvec(dnums.Size());
	    GetElementVector (dnums, elvec);
	    
	    for( int j = 0; j < dnums.Size(); j++)
	      data.Append(elvec(j));
	  }    
	
	MyMPI_Gather (nodenums.Size());
	MyMPI_Gather (data.Size());

	MyMPI_Send(nodenums,0,22);
	MyMPI_Send(data,0,23);
      }
    else
      {
	Array<Vec<N,int> > points(0);
	Array<Vec<2,int> > positions(0);
	

	Array<int> size_nodes(ntasks), size_data(ntasks);
	MyMPI_GatherRoot (size_nodes);
	MyMPI_GatherRoot (size_data);

	Array<MPI_Request> requests;

	Table<Vec<N+1,int> > table_nodes(size_nodes);
	for (int p = 1; p < ntasks; p++)
	  requests.Append (MyMPI_IRecv (table_nodes[p], p, 22));

	Table<SCAL> table_data(size_data);
	for (int p = 1; p < ntasks; p++)
	  requests.Append (MyMPI_IRecv (table_data[p], p, 23));
	MyMPI_WaitAll (requests);

	FlatArray<SCAL> data = table_data.AsArray();


	int size = 0;
	for( int proc = 1; proc < ntasks; proc++)
	  {
	    FlatArray<Vec<N+1,int> > locpoints = table_nodes[proc];
	    Vec<N,int>  temp;
	    
	    for( int j = 0; j < locpoints.Size(); j++ )
	      {
		int nodesize = locpoints[j][N];
		
		positions.Append (Vec<2,int> (size, nodesize));
		
		for( int k = 0; k < N; k++)
		  temp[k] = locpoints[j][k];
		
		points.Append( temp );
		size += nodesize;
	      }
	  }    
	
	Array<int> index(points.Size());
	for( int i = 0; i < index.Size(); i++) index[i] = i;
	
	static Timer ts ("Save Gridfunction, sort");
	static Timer tw ("Save Gridfunction, write");
	ts.Start();
	QuickSortI (points, index, MyLess<N>);
	ts.Stop();

	tw.Start();
	for( int i = 0; i < points.Size(); i++)
	  {
	    int start = positions[index[i]][0];
	    int end = positions[index[i]][1];
	    
	    for( int j = 0; j < end; j++)
	      SaveBin<SCAL>(ost, data[start++]);
	  }
	tw.Stop();	
      }
#endif	   
  }





  template <class SCAL>
  S_ComponentGridFunction<SCAL> :: 
  S_ComponentGridFunction (const S_GridFunction<SCAL> & agf_parent, int acomp)
    : S_GridFunction<SCAL> (dynamic_cast<const CompoundFESpace&> (*agf_parent.GetFESpace())[acomp], 
			    agf_parent.GetName()+"."+ToString (acomp+1), Flags()), 
      gf_parent(agf_parent), comp(acomp)
  { 
    this->SetVisual(agf_parent.GetVisual());
    const CompoundFESpace * cfe = dynamic_cast<const CompoundFESpace *>(this->GetFESpace().get());
    if (cfe)
      {
	int nsp = cfe->GetNSpaces();
	this->compgfs.SetSize(nsp);
	for (int i = 0; i < nsp; i++)
	  this->compgfs[i] = make_shared<S_ComponentGridFunction<SCAL>> (*this, i);
      }
    
    // this->Visualize (this->name);
    if (this->visual)
      Visualize (shared_ptr<GridFunction> (this, NOOP_Deleter), this->name);
  }

  template <class SCAL>
  S_ComponentGridFunction<SCAL> :: 
  ~S_ComponentGridFunction ()
  {
    this -> vec = NULL;  // base-class desctructor must not delete the vector
  }


  template <class SCAL>
  void S_ComponentGridFunction<SCAL> :: Update()
  {
    const CompoundFESpace & cfes = dynamic_cast<const CompoundFESpace&> (*gf_parent.GetFESpace().get());

    this -> vec.SetSize (gf_parent.GetMultiDim());
    for (int i = 0; i < gf_parent.GetMultiDim(); i++)
      (this->vec)[i] = gf_parent.GetVector(i).Range (cfes.GetRange(comp));
  
    this -> level_updated = this -> ma->GetNLevels();

    for (int i = 0; i < this->compgfs.Size(); i++)
      this->compgfs[i]->Update();
  }


  /*
  template <class TV>
  T_GridFunction<TV> ::
  T_GridFunction (const FESpace & afespace, const string & aname, const Flags & flags)
    : S_GridFunction<TSCAL> (afespace, aname, flags)
  {
    vec.SetSize (this->multidim);
    vec = 0;

    const CompoundFESpace * cfe = dynamic_cast<const CompoundFESpace *>(&this->GetFESpace());
    if (cfe)
      {
	int nsp = cfe->GetNSpaces();
	compgfs.SetSize(nsp);
	for (int i = 0; i < nsp; i++)
	  compgfs[i] = new S_ComponentGridFunction<TSCAL> (*this, i);
      }    

    this->Visualize (this->name);
  }
  */

  template <class TV>
  T_GridFunction<TV> ::
  T_GridFunction (const FESpace & afespace, const string & aname, const Flags & flags)
    : T_GridFunction(shared_ptr<FESpace> (const_cast<FESpace*>(&afespace),NOOP_Deleter), aname, flags)
  { ; }

  template <class TV>
  T_GridFunction<TV> ::
  T_GridFunction (shared_ptr<FESpace> afespace, const string & aname, const Flags & flags)
    : S_GridFunction<TSCAL> (afespace, aname, flags)
  {
    vec.SetSize (this->multidim);
    vec = 0;

    const CompoundFESpace * cfe = dynamic_cast<const CompoundFESpace *>(this->GetFESpace().get());
    if (cfe)
      {
	int nsp = cfe->GetNSpaces();
	compgfs.SetSize(nsp);
	for (int i = 0; i < nsp; i++)
	  compgfs[i] = make_shared<S_ComponentGridFunction<TSCAL>> (*this, i);
      }    

    // this->Visualize (this->name);
    if (this->visual)
      Visualize(shared_ptr<GridFunction> (this, NOOP_Deleter), this->name);
  }



  template <class TV>
  T_GridFunction<TV> :: ~T_GridFunction()
  {
    ;
  }


  template <class TV>
  void T_GridFunction<TV> :: Update () 
  {
    try
      {
        if (this->GetFESpace()->GetLevelUpdated() < this->ma->GetNLevels())
          {
            LocalHeap llh (1000000, "gridfunction update");
            this->GetFESpace()->Update (llh);
            this->GetFESpace()->FinalizeUpdate (llh);
          }



	int ndof = this->GetFESpace()->GetNDof();

	for (int i = 0; i < this->multidim; i++)
	  {
	    if (vec[i] && ndof == vec[i]->Size())
	      break;
	    
	    shared_ptr<BaseVector> ovec = vec[i];
	
#ifdef PARALLEL
	    if ( & this->GetFESpace()->GetParallelDofs() )
	      vec[i] = make_shared<ParallelVVector<TV>> (ndof, &this->GetFESpace()->GetParallelDofs(), CUMULATED);
	    else
#endif
 	      vec[i] = make_shared<VVector<TV>> (ndof);
	    
            
	    *vec[i] = TSCAL(0);

	    if (this->nested && ovec && this->GetFESpace()->GetProlongation())
	      {
		*vec[i]->Range (0, ovec->Size()) += *ovec;

		const_cast<ngmg::Prolongation&> (*this->GetFESpace()->GetProlongation()).Update();
		
		this->GetFESpace()->GetProlongation()->ProlongateInline
		  (this->GetMeshAccess()->GetNLevels()-1, *vec[i]);
	      }

	    //	    if (i == 0)
            // cout << "visualize" << endl;
            // Visualize (this->name);
	    
	    // delete ovec;
	  }
	
	this -> level_updated = this -> ma->GetNLevels();

	// const CompoundFESpace * cfe = dynamic_cast<const CompoundFESpace *>(&GridFunction :: GetFESpace());
	// if (cfe)
	for (int i = 0; i < compgfs.Size(); i++)
	  compgfs[i]->Update();
      }
    catch (Exception & e)
      {
	e.Append ("In GridFunction::Update()\n");
	throw e;
      }    
    catch (exception & e)
      {
	Exception e2 (e.what());
	e2.Append ("\nIn GridFunction::Update()\n");
	throw e2;
      }
  }

  /*
    sharedGridFunction * CreateGridFunction (const FESpace * space,
  const string & name, const Flags & flags)
  {
    return CreateGridFunction (shared_ptr<FESpace> (const_cast<FESpace*>(space), NOOP_Deleter), name, flags);
  }
  */

  shared_ptr<GridFunction> CreateGridFunction (shared_ptr<FESpace> space,
                                               const string & name, const Flags & flags)
  {
    shared_ptr<GridFunction> gf 
      ((GridFunction*)CreateVecObject  <T_GridFunction, GridFunction> // , const FESpace, const string, const Flags>
       (space->GetDimension() * int(flags.GetNumFlag("cacheblocksize",1)), 
       space->IsComplex(), space, name, flags));
  
    gf->SetCacheBlockSize(int(flags.GetNumFlag("cacheblocksize",1)));
    
    return gf;
  }












  GridFunctionCoefficientFunction :: 
  GridFunctionCoefficientFunction (shared_ptr<GridFunction> agf, int acomp)
    : CoefficientFunction(1, agf->GetFESpace()->IsComplex()), gf(agf) /*, diffop (NULL)*/ , comp (acomp) 
  {
    fes = gf->GetFESpace();
    SetDimensions (gf->Dimensions());
    /*
    diffop = gf->GetFESpace()->GetEvaluator(VOL);
    trace_diffop = gf->GetFESpace()->GetEvaluator(BND);
    ttrace_diffop = gf->GetFESpace()->GetEvaluator(BBND);
    */
    for (auto vb : { VOL, BND, BBND })
      diffop[vb] = gf->GetFESpace()->GetEvaluator(vb);
  }

  GridFunctionCoefficientFunction :: 
  GridFunctionCoefficientFunction (shared_ptr<DifferentialOperator> adiffop,
                                   shared_ptr<DifferentialOperator> atrace_diffop,
				   shared_ptr<DifferentialOperator> attrace_diffop,
                                   int acomp)
    : CoefficientFunction(1, false),
      // diffop (adiffop), trace_diffop(atrace_diffop), ttrace_diffop(attrace_diffop),
      diffop{adiffop,atrace_diffop, attrace_diffop},
      comp (acomp) 
  {
    ; // SetDimensions (gf->Dimensions());

    for (auto vb : { VOL, BND, BBND } )
      if (diffop[vb])
        {
          SetDimensions (diffop[vb]->Dimensions());
          break;
        }
    /*
    if (diffop)
      SetDimensions (diffop->Dimensions());
    else if (trace_diffop)
      SetDimensions (trace_diffop->Dimensions());
    else if (ttrace_diffop)
      SetDimensions (ttrace_diffop->Dimensions());
    */
  }

  GridFunctionCoefficientFunction :: 
  GridFunctionCoefficientFunction (shared_ptr<GridFunction> agf,
				   shared_ptr<DifferentialOperator> adiffop,
                                   shared_ptr<DifferentialOperator> atrace_diffop,
				   shared_ptr<DifferentialOperator> attrace_diffop,
                                   int acomp)
    : CoefficientFunction(1,agf->IsComplex()),
      gf(agf),
      // diffop (adiffop), trace_diffop(atrace_diffop), ttrace_diffop(attrace_diffop),
      diffop{adiffop,atrace_diffop,attrace_diffop},
      comp (acomp) 
  {
    fes = gf->GetFESpace();
    for (auto vb : { VOL, BND, BBND } )
      if (diffop[vb])
        {
          SetDimensions (diffop[vb]->Dimensions());
          break;
        }
    /*
    if (diffop)
      SetDimensions (diffop->Dimensions());
    else if (trace_diffop)
      SetDimensions (trace_diffop->Dimensions());
    else if (ttrace_diffop)
      SetDimensions (ttrace_diffop->Dimensions());
    */
  }
  
  GridFunctionCoefficientFunction :: 
  GridFunctionCoefficientFunction (shared_ptr<GridFunction> agf, 
				   shared_ptr<BilinearFormIntegrator> abfi, int acomp)
    : CoefficientFunction(1, agf->IsComplex()),
      gf(agf), /* bfi (abfi), */ comp (acomp) 
  {
    fes = gf->GetFESpace();
    SetDimensions (gf->Dimensions());
    diffop[abfi->VB()] = make_shared<CalcFluxDifferentialOperator> (abfi, false);
    /*
    if (bfi->VB() == VOL)
    diffop = make_shared<CalcFluxDifferentialOperator> (bfi, false);
    else if (bfi->VB() == BND)
      diffop = make_shared<CalcFluxDifferentialOperator> (bfi, false);      
    */
  }


  GridFunctionCoefficientFunction :: 
  ~GridFunctionCoefficientFunction ()
  {
    ;
  }

  int GridFunctionCoefficientFunction::Dimension() const
  {
    for (auto vb : { VOL, BND, BBND })
      if (diffop[vb])
        return diffop[vb]->Dim();
    /*
    if (diffop) return diffop->Dim();
    if (trace_diffop) return trace_diffop->Dim();
    if (bfi) return bfi->DimFlux();
    if (gf->GetFESpace()->GetEvaluator())
      return gf->GetFESpace()->GetEvaluator()->Dim();
    */
    throw Exception(string ("don't know my dimension, space is ") +
                    typeid(*gf->GetFESpace()).name());
  }

  Array<int> GridFunctionCoefficientFunction::Dimensions() const
  {
    for (auto vb : { VOL, BND, BBND })
      if (diffop[vb])
        return Array<int> (diffop[vb]->Dimensions());
    /*
    if (diffop)
      return Array<int> (diffop->Dimensions());
    else if (trace_diffop)
      return Array<int> (trace_diffop->Dimensions());
    else if (ttrace_diffop)
      return Array<int> (ttrace_diffop->Dimensions());
    */
    // is it possible ?? 
    return Array<int> ( { Dimension() } );
    /*
    int d = Dimension();
    if (diffop)
      {
        int spacedim = gf->GetFESpace()->GetDimension();
        if (spacedim > 1)
          return Array<int> ( { spacedim, d/spacedim } );
      }
    return Array<int>( { d } );
    */
  }

  void GridFunctionCoefficientFunction :: GenerateCode(Code &code, FlatArray<int> inputs, int index) const
  {
    string mycode_simd = R"CODE_( 
      STACK_ARRAY(SIMD<double>, {hmem}, mir.Size()*{dim});
      AFlatMatrix<double>  {values}({dim}, mir.IR().GetNIP(), &{hmem}[0] /* .Data() */);
      {
      auto gfcf = reinterpret_cast<GridFunctionCoefficientFunction*>({gfcf_ptr});
      ProxyUserData * ud = (ProxyUserData*)mir.GetTransformation().userdata;
      if (ud && ud->HasMemory(gfcf) && ud->Computed(gfcf))
        {
          {values} = AFlatMatrix<double> ({dim}, mir.IR().GetNIP(), &ud->GetAMemory(gfcf)(0,0));
        }
      else
        {
      const GridFunction & gf = *reinterpret_cast<GridFunction*>({gf_ptr});
      const ElementTransformation &trafo = mir.GetTransformation();
      auto elnr = trafo.GetElementNr();
      // const FESpace &fes = *gf.GetFESpace();
      const FESpace & fes = *reinterpret_cast<FESpace*>({fes_ptr});
      auto vb = trafo.VB();
      ElementId ei(vb, elnr);
      // DifferentialOperator * diffop = (DifferentialOperator*){diffop_ptr};
      // DifferentialOperator * trace_diffop = (DifferentialOperator*){trace_diffop_ptr};
      DifferentialOperator * diffop[3] = { (DifferentialOperator*){diffop_vol_ptr}, (DifferentialOperator*){diffop_bnd_ptr}, (DifferentialOperator*){diffop_bbnd_ptr}};
      // BilinearFormIntegrator * bfi = (BilinearFormIntegrator*){bfi_ptr};
      if (!trafo.BelongsToMesh((void*)(fes.GetMeshAccess().get()))) {
          throw Exception ("SIMD - evaluation not available for different meshes");
      } else if(!fes.DefinedOn(vb,trafo.GetElementIndex())){
          {values} = 0.0;
      } else {
          const FiniteElement & fel = fes.GetFE (ei, gridfunction_local_heap);
          int dim = fes.GetDimension();

          auto &dnums = gridfunction_dnums;
          fes.GetDofNrs (ei, dnums);

          gridfunction_elu.SetSize(dnums.Size()*dim);
          FlatVector<> elu(dnums.Size()*dim, &gridfunction_elu[0]);

          gf.GetElementVector ({comp}, dnums, elu);
          fes.TransformVec (ei, elu, TRANSFORM_SOL);
/*
          if (diffop && vb==VOL)
            diffop->Apply (fel, mir, elu, {values});
          else if (trace_diffop && vb==BND)
            trace_diffop->Apply (fel, mir, elu, {values});
          else if (bfi)
            throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 1");
          // bfi->CalcFlux (fel, mir, elu, values, true, gridfunction_local_heap);
          else if (fes.GetEvaluator(vb))
            fes.GetEvaluator(vb) -> Apply (fel, mir, elu, {values}); // , gridfunction_local_heap);
          else if (fes.GetIntegrator(vb))
            throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 2");
          // fes.GetIntegrator(vb) ->CalcFlux (fel, mir, elu, values, false, gridfunction_local_heap);
*/
          if (diffop[vb])
             diffop[vb]->Apply (fel, mir, elu, {values});
          else
            throw Exception ("GridFunctionCoefficientFunction: SIMD: don't know how I shall evaluate");
        if (ud)
          {
            if (ud->HasMemory(gfcf))
              {
                ud->GetAMemory(gfcf) = BareSliceMatrix<SIMD<double>> ({values});
                ud->SetComputed(gfcf);
              }
          }    
        }
      }
      }
    )CODE_";
    string mycode = R"CODE_( 
      // Matrix<>  {values}{mir.Size(), {dim}};
      STACK_ARRAY(double, {hmem}, mir.Size()*{dim});
      FlatMatrix<double>  {values}(mir.Size(), {dim}, &{hmem}[0]);
      {
      const GridFunction & gf = *reinterpret_cast<GridFunction*>({gf_ptr});
      const ElementTransformation &trafo = mir.GetTransformation();
      auto elnr = trafo.GetElementNr();
      const FESpace &fes = *gf.GetFESpace();
      auto vb = trafo.VB();
      ElementId ei(vb, elnr);
      // DifferentialOperator * diffop = (DifferentialOperator*){diffop_ptr};
      // DifferentialOperator * trace_diffop = (DifferentialOperator*){trace_diffop_ptr};
      // DifferentialOperator * trace_diffop[3] = (DifferentialOperator*){diffop_vol,diffop_bnd,diffop_bbnd};
      DifferentialOperator * diffop[3] = { (DifferentialOperator*){diffop_vol_ptr}, (DifferentialOperator*){diffop_bnd_ptr}, (DifferentialOperator*){diffop_bbnd_ptr}};
      // BilinearFormIntegrator * bfi = (BilinearFormIntegrator*){bfi_ptr};
      if (!trafo.BelongsToMesh((void*)(fes.GetMeshAccess().get()))) {
          gf.Evaluate(mir, {values});
          //for (auto i : Range(mir.Size()))
          //  gf.Evaluate(mir[i], {values}.Row(i));
      } else if(!fes.DefinedOn(vb,trafo.GetElementIndex())){
          {values} = 0.0;
      } else {
          const FiniteElement & fel = fes.GetFE (ei, gridfunction_local_heap);
          int dim = fes.GetDimension();

          auto &dnums = gridfunction_dnums;
          fes.GetDofNrs (ei, dnums);

          gridfunction_elu.SetSize(dnums.Size()*dim);
          FlatVector<> elu(dnums.Size()*dim, &gridfunction_elu[0]);

          gf.GetElementVector ({comp}, dnums, elu);
          fes.TransformVec (ei, elu, TRANSFORM_SOL);
/*
          if (diffop && vb==VOL)
            diffop->Apply (fel, mir, elu, {values}, gridfunction_local_heap);
          else if (trace_diffop && vb==BND)
            trace_diffop->Apply (fel, mir, elu, {values}, gridfunction_local_heap);
          else if (bfi)
            bfi->CalcFlux (fel, mir, elu, {values}, true, gridfunction_local_heap);
          else if (fes.GetEvaluator(vb))
            fes.GetEvaluator(vb) -> Apply (fel, mir, elu, {values}, gridfunction_local_heap);
          else if (fes.GetIntegrator(vb))
            fes.GetIntegrator(vb) ->CalcFlux (fel, mir, elu, {values}, false, gridfunction_local_heap);
*/
          if (diffop[vb])
             diffop[vb]->Apply (fel, mir, elu, {values}, gridfunction_local_heap);
          else
            throw Exception ("don't know how I shall evaluate");
      }
      }
    )CODE_";
    std::map<string,string> variables;
    auto values = Var("values", index);
    variables["values"] = values.S();
    variables["gf_ptr"] = ToString(gf.get());
    variables["gfcf_ptr"] = ToString(this);
    variables["fes_ptr"] = ToString(fes.get());
    // variables["diffop_ptr"] = ToString(diffop[VOL].get());
    // variables["trace_diffop_ptr"] = ToString(diffop[BND].get());
    // variables["bfi_ptr"] = ToString(trace_diffop.get());
    variables["diffop_vol_ptr"] = ToString(diffop[VOL].get());
    variables["diffop_bnd_ptr"] = ToString(diffop[BND].get());
    variables["diffop_bbnd_ptr"] = ToString(diffop[BBND].get());
    variables["comp"] = ToString(comp);
    variables["dim"] = ToString(Dimension());
    variables["hmem"] = Var("hmem", index).S();

    if(code.is_simd)
      {
        code.header += Code::Map(mycode_simd, variables);
        TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign("SIMD<double>("+values.S()+".Get("+ToString(ind)+",i))");
          });
      }
    else
      {
        code.header += Code::Map(mycode, variables);
        TraverseDimensions( Dimensions(), [&](int ind, int i, int j) {
            code.body += Var(index,i,j).Assign(values.S()+"(i,"+ToString(ind)+")");
          });
      }
  }
  
  bool GridFunctionCoefficientFunction::IsComplex() const
  {
    return gf->GetFESpace()->IsComplex(); 
  }

  double GridFunctionCoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationPoint & ip) const
  {
    VectorMem<10> flux(Dimension());
    Evaluate (ip, flux);
    return flux(0);
  }
  
  Complex GridFunctionCoefficientFunction :: 
  EvaluateComplex (const BaseMappedIntegrationPoint & ip) const
  {
    VectorMem<10,Complex> flux(Dimension());
    Evaluate (ip, flux);
    return flux(0);
  }



  void GridFunctionCoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationPoint & ip, FlatVector<> result) const
  {
    LocalHeapMem<100000> lh2 ("GridFunctionCoefficientFunction, Eval 2");
    static Timer timer ("GFCoeffFunc::Eval-scal", 3);
    RegionTimer reg (timer);

    const ElementTransformation & trafo = ip.GetTransformation();
    
    // int elnr = trafo.GetElementNr();
    // VorB vb  = trafo.VB();
    // ElementId ei(vb, elnr);
    ElementId ei = trafo.GetElementId();

    // auto fes = gf->GetFESpace();
    const shared_ptr<MeshAccess> & ma = fes->GetMeshAccess();

    if (gf->GetLevelUpdated() != ma->GetNLevels())
    {
      result = 0.0;
      return;
    }
    
    if (!trafo.BelongsToMesh (ma.get()))
      {
        IntegrationPoint rip;
        int elnr2 = ma->FindElementOfPoint 
          // (static_cast<const DimMappedIntegrationPoint<2>&> (ip).GetPoint(),
          (ip.GetPoint(), rip, true);  // buildtree not yet threadsafe (maybe now ?)
        if (elnr2 == -1)
          {
            result = 0;
            return;
          }
        const ElementTransformation & trafo2 = ma->GetTrafo(ElementId(ei.VB(), elnr2), lh2);
        return Evaluate (trafo2(rip, lh2), result);
      }
    
    if (!fes->DefinedOn (ei.VB(),trafo.GetElementIndex()))
      { 
        result = 0.0; 
        return;
      }
    
    const FiniteElement & fel = fes->GetFE (ei, lh2);
    int dim = fes->GetDimension();
    
    ArrayMem<int, 50> dnums;
    fes->GetDofNrs (ei, dnums);
    
    VectorMem<50> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes->TransformVec (ei, elu, TRANSFORM_SOL);
    if (diffop[ei.VB()])
      diffop[ei.VB()] -> Apply(fel, ip, elu, result, lh2);
    /*
    if (diffop && ei.VB()==VOL)
      diffop->Apply (fel, ip, elu, result, lh2);
    else if (trace_diffop && ei.VB()==BND)
      trace_diffop->Apply (fel, ip, elu, result, lh2);
    else if (bfi)
      bfi->CalcFlux (fel, ip, elu, result, true, lh2);
    else if (fes->GetEvaluator(ei.VB()))
      fes->GetEvaluator(ei.VB()) -> Apply (fel, ip, elu, result, lh2);
    */
    else
      result = 0.0;
  }

  void GridFunctionCoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationPoint & ip, FlatVector<Complex> result) const
  {
    LocalHeapMem<100000> lh2 ("GridFunctionCoefficientFunction, Eval complex");
    static Timer timer ("GFCoeffFunc::Eval-scal", 3);
    RegionTimer reg (timer);

    
    const int elnr = ip.GetTransformation().GetElementNr();
    VorB vb = ip.GetTransformation().VB();
    ElementId ei(vb, elnr);

    // const FESpace & fes = *gf->GetFESpace();
    shared_ptr<MeshAccess>  ma = fes->GetMeshAccess();
    
    if (!ip.GetTransformation().BelongsToMesh (ma.get()))
      {
        IntegrationPoint rip;
        int elnr = ma->FindElementOfPoint(ip.GetPoint(), rip, true); // buildtree not yet threadsafe (maybe now ?)
        if (elnr == -1)
          {
            result = 0;
            return;
          }
        const ElementTransformation & trafo2 = ma->GetTrafo(ElementId(vb, elnr), lh2);
        Evaluate (trafo2(rip, lh2), result);
        return;
      }

    if (!fes->DefinedOn (vb,ip.GetTransformation().GetElementIndex()))
      { 
        result = 0.0; 
        return;
      }
    
    const FiniteElement & fel = fes->GetFE (ei, lh2);
    int dim = fes->GetDimension();
    
    ArrayMem<int, 50> dnums;
    fes->GetDofNrs (ei, dnums);
    
    VectorMem<50, Complex> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes->TransformVec (ei, elu, TRANSFORM_SOL);

    if (diffop[vb])
      diffop[vb]->Apply (fel, ip, elu, result, lh2);
    else
      result = 0.0;
    /*
    if (diffop && vb==VOL)
      diffop->Apply (fel, ip, elu, result, lh2);
    else if (trace_diffop && vb==BND)
      trace_diffop->Apply (fel, ip, elu, result, lh2);
    else if (bfi)
      bfi->CalcFlux (fel, ip, elu, result, true, lh2);
    else
      fes->GetIntegrator(vb) -> CalcFlux (fel, ip, elu, result, false, lh2);
    */
  }


  void GridFunctionCoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationRule & ir, FlatMatrix<double> values) const
  {
    LocalHeapMem<100000> lh2("GridFunctionCoefficientFunction - Evalute 3");
    // static Timer timer ("GFCoeffFunc::Eval-vec", 2);
    // RegionTimer reg (timer);
    const ElementTransformation & trafo = ir.GetTransformation();
    
    int elnr = trafo.GetElementNr();
    VorB vb = trafo.VB();
    ElementId ei(vb, elnr);

    // const FESpace & fes = *gf->GetFESpace();

    if (!trafo.BelongsToMesh ((void*)(fes->GetMeshAccess().get())))
      {
        for (int i = 0; i < ir.Size(); i++)
          Evaluate (ir[i], values.Row(i));
        return;
      }
    
    if (!fes->DefinedOn(vb, trafo.GetElementIndex())) 
      { 
        values = 0.0; 
        return;
      }

    const FiniteElement & fel = fes->GetFE (ei, lh2);
    int dim = fes->GetDimension();

    ArrayMem<int, 50> dnums;
    fes->GetDofNrs (ei, dnums);
    
    VectorMem<50> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes->TransformVec (ElementId(vb, elnr), elu, TRANSFORM_SOL);

    if (diffop[vb])
      diffop[vb]->Apply (fel, ir, elu, values, lh2);
    /*
    if (diffop && vb==VOL)
      diffop->Apply (fel, ir, elu, values, lh2);
    else if (trace_diffop && vb==BND)
      trace_diffop->Apply (fel, ir, elu, values, lh2);
    else if (bfi)
      bfi->CalcFlux (fel, ir, elu, values, true, lh2);
    else if (fes->GetEvaluator(vb))
      fes->GetEvaluator(vb) -> Apply (fel, ir, elu, values, lh2);
    else if (fes->GetIntegrator(vb))
      fes->GetIntegrator(vb) ->CalcFlux (fel, ir, elu, values, false, lh2);
    */
    else
      throw Exception ("don't know how I shall evaluate");
  }

  void GridFunctionCoefficientFunction :: 
  Evaluate (const BaseMappedIntegrationRule & ir, FlatMatrix<Complex> values) const
  {
    LocalHeapMem<100000> lh2("GridFunctionCoefficientFunction - Evalute 3");
    // static Timer timer ("GFCoeffFunc::Eval-vec", 2);
    // RegionTimer reg (timer);

    const ElementTransformation & trafo = ir.GetTransformation();
    
    int elnr = trafo.GetElementNr();
    VorB vb = trafo.VB();
    ElementId ei(vb, elnr);

    // const FESpace & fes = *gf->GetFESpace();

    if (!trafo.BelongsToMesh ((void*)(fes->GetMeshAccess().get())))
      {
        for (int i = 0; i < ir.Size(); i++)
          Evaluate (ir[i], values.Row(i));
        return;
      }
    
    if (!fes->DefinedOn(vb, trafo.GetElementIndex())) 
      { 
        values = 0.0; 
        return;
      }
    
    const FiniteElement & fel = fes->GetFE (ei, lh2);
    int dim = fes->GetDimension();

    ArrayMem<int, 50> dnums;
    fes->GetDofNrs (ei, dnums);
    
    VectorMem<50,Complex> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes->TransformVec (ei, elu, TRANSFORM_SOL);

    /*
    if (diffop && vb==VOL)
      diffop->Apply (fel, ir, elu, values, lh2);
    else if (trace_diffop && vb==BND)
      trace_diffop->Apply (fel, ir, elu, values, lh2);
    else if (bfi)
      bfi->CalcFlux (fel, ir, elu, values, true, lh2);
    else if (fes->GetEvaluator(vb))
      fes->GetEvaluator(vb) -> Apply (fel, ir, elu, values, lh2);
    else if (fes->GetIntegrator(vb))
      fes->GetIntegrator(vb) ->CalcFlux (fel, ir, elu, values, false, lh2);
    */
    if (diffop[vb])
      diffop[vb]->Apply (fel, ir, elu, values, lh2);
    else
      throw Exception ("don't know how I shall evaluate");
  }

  void GridFunctionCoefficientFunction ::   
  Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
            BareSliceMatrix<SIMD<double>> bvalues) const
  {
    ProxyUserData * ud = (ProxyUserData*)ir.GetTransformation().userdata;
    if (ud)
      {
        if (ud->HasMemory(this) && ud->Computed(this))
          {
            bvalues.AddSize(Dimension(), ir.Size()) = ud->GetAMemory(this);
            return;
          }
      }
    

    
    LocalHeapMem<100000> lh2("GridFunctionCoefficientFunction - Evalute 3");
    // static Timer timer ("GFCoeffFunc::Eval-vec", 2);
    // RegionTimer reg (timer);
    auto values = bvalues.AddSize(Dimension(), ir.Size());
    const ElementTransformation & trafo = ir.GetTransformation();
    
    int elnr = trafo.GetElementNr();
    VorB vb = trafo.VB();
    ElementId ei(vb, elnr);

    // const FESpace & fes = *gf->GetFESpace();

    if (!trafo.BelongsToMesh ((void*)(fes->GetMeshAccess().get())))
      {
        throw ExceptionNOSIMD ("SIMD - evaluation not available for different meshes");
        // for (int i = 0; i < ir.Size(); i++)
        // Evaluate (ir[i], values.Row(i));
        return;
      }
    
    if (!fes->DefinedOn(vb,trafo.GetElementIndex())) 
      { 
        values = 0.0; 
        return;
      }
    
    const FiniteElement & fel = fes->GetFE (ei, lh2);
    int dim = fes->GetDimension();

    ArrayMem<int, 50> dnums;
    fes->GetDofNrs (ei, dnums);
    
    VectorMem<50> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes->TransformVec (ei, elu, TRANSFORM_SOL);
    /*
    if (diffop && vb==VOL)
      diffop->Apply (fel, ir, elu, values); // , lh2);
    else if (trace_diffop && vb==BND)
      trace_diffop->Apply (fel, ir, elu, values); // , lh2);
    else if (bfi)
      throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 1");
      // bfi->CalcFlux (fel, ir, elu, values, true, lh2);
    else if (fes->GetEvaluator(vb))
      fes->GetEvaluator(vb) -> Apply (fel, ir, elu, values); // , lh2);
    else if (fes->GetIntegrator(vb))
      throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 2");
      // fes.GetIntegrator(boundary) ->CalcFlux (fel, ir, elu, values, false, lh2);
    */
    if (diffop[vb])
      diffop[vb]->Apply (fel, ir, elu, values);
    else
      throw Exception ("GridFunctionCoefficientFunction: SIMD: don't know how I shall evaluate");


    if (ud)
      {
        if (ud->HasMemory(this))
          {
            ud->GetAMemory(this) = bvalues;
            ud->SetComputed(this);
          }
      }    
  }

  void GridFunctionCoefficientFunction ::   
  Evaluate (const SIMD_BaseMappedIntegrationRule & ir,
            BareSliceMatrix<SIMD<Complex>> bvalues) const
  {
    LocalHeapMem<100000> lh2("GridFunctionCoefficientFunction - Evalute 3");

    auto values = bvalues.AddSize(Dimension(), ir.Size());
    const ElementTransformation & trafo = ir.GetTransformation();
    
    int elnr = trafo.GetElementNr();
    VorB vb = trafo.VB();
    ElementId ei(vb, elnr);

    // const FESpace & fes = *gf->GetFESpace();
    const FESpace & fes = *this->fes;

    if (!trafo.BelongsToMesh ((void*)(fes.GetMeshAccess().get())))
      {
        throw ExceptionNOSIMD ("SIMD - evaluation not available for different meshes");
        // for (int i = 0; i < ir.Size(); i++)
        // Evaluate (ir[i], values.Row(i));
        return;
      }
    
    if (!fes.DefinedOn(vb, trafo.GetElementIndex())) 
      { 
        values = 0.0; 
        return;
      }
    
    const FiniteElement & fel = fes.GetFE (ei, lh2);
    int dim = fes.GetDimension();

    ArrayMem<int, 50> dnums;
    fes.GetDofNrs (ei, dnums);
    
    VectorMem<50, Complex> elu(dnums.Size()*dim);

    gf->GetElementVector (comp, dnums, elu);
    fes.TransformVec (ei, elu, TRANSFORM_SOL);
    /*
    if (diffop && vb==VOL)
      diffop->Apply (fel, ir, elu, values); // , lh2);
    else if (trace_diffop && vb==BND)
      trace_diffop->Apply (fel, ir, elu, values); // , lh2);
    else if (ttrace_diffop && vb==BBND)
      ttrace_diffop->Apply(fel,ir,elu,values);
    else if (bfi)
      throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 1");
      // bfi->CalcFlux (fel, ir, elu, values, true, lh2);
    else if (fes.GetEvaluator(vb))
      fes.GetEvaluator(vb) -> Apply (fel, ir, elu, values); // , lh2);
    else if (fes.GetIntegrator(vb))
      throw Exception ("GridFunctionCoefficientFunction: SIMD evaluate not possible 2");
      // fes.GetIntegrator(boundary) ->CalcFlux (fel, ir, elu, values, false, lh2);
      */
    if (diffop[vb])
      diffop[vb]->Apply (fel, ir, elu, values);    
    else
      throw Exception ("GridFunctionCoefficientFunction: SIMD: don't know how I shall evaluate");    
  }







  

  template <class SCAL>
  VisualizeGridFunction<SCAL> ::
  VisualizeGridFunction (shared_ptr<MeshAccess>  ama,
			 shared_ptr<GridFunction> agf,
			 shared_ptr<BilinearFormIntegrator> abfi2d,
			 shared_ptr<BilinearFormIntegrator> abfi3d,
			 bool aapplyd)

    : SolutionData (agf->GetName(), -1, agf->GetFESpace()->IsComplex()),
      ma(ama), gf(dynamic_pointer_cast<S_GridFunction<SCAL>> (agf)), 
      applyd(aapplyd) // , lh(10000013, "VisualizedGridFunction 2")
  { 
    if(abfi2d)
      bfi2d.Append(abfi2d);
    if(abfi3d)
      bfi3d.Append(abfi3d);

    if (abfi2d) components = abfi2d->DimFlux();
    if (abfi3d) components = abfi3d->DimFlux();
    if (iscomplex) components *= 2;
    multidimcomponent = 0;
  }

  template <class SCAL>
  VisualizeGridFunction<SCAL> ::
  VisualizeGridFunction (shared_ptr<MeshAccess>  ama,
			 shared_ptr<GridFunction> agf,
			 const Array<shared_ptr<BilinearFormIntegrator>> & abfi2d,
			 const Array<shared_ptr<BilinearFormIntegrator>> & abfi3d,
			 bool aapplyd)

    : SolutionData (agf->GetName(), -1, agf->GetFESpace()->IsComplex()),
      ma(ama), gf(dynamic_pointer_cast<S_GridFunction<SCAL>> (agf)), 
      applyd(aapplyd) // , lh(10000002, "VisualizeGridFunction")
  { 
    for(int i=0; i<abfi2d.Size(); i++)
      bfi2d.Append(abfi2d[i]);
    for(int i=0; i<abfi3d.Size(); i++)
      bfi3d.Append(abfi3d[i]);
    

    if (bfi2d.Size()) components = bfi2d[0]->DimFlux();
    if (bfi3d.Size()) components = bfi3d[0]->DimFlux();

    if (iscomplex) components *= 2;
    multidimcomponent = 0;
  }

  template <class SCAL>
  VisualizeGridFunction<SCAL> :: ~VisualizeGridFunction ()
  {
    ;
  }
  

  template <class SCAL>
  bool VisualizeGridFunction<SCAL> :: 
  GetValue (int elnr, double lam1, double lam2, double lam3,
            double * values) 
  { 
    // static Timer t("visgf::GetValue");
    // RegionTimer reg(t);
    // t.AddFlops (1);

    try
      {
	LocalHeapMem<100000> lh("visgf::getvalue");
	if (!bfi3d.Size()) return false;
	if (gf -> GetLevelUpdated() < ma->GetNLevels()) return false;
	const FESpace & fes = *gf->GetFESpace();

	int dim     = fes.GetDimension();
        ElementId ei(VOL,elnr);

	if ( !fes.DefinedOn(ei)) return false;

	HeapReset hr(lh);
	
	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
	const FiniteElement & fel = fes.GetFE (ei, lh);

	ArrayMem<int,200> dnums (fel.GetNDof());
	fes.GetDofNrs (ei, dnums);

	VectorMem<200,SCAL> elu(dnums.Size() * dim);
	if(gf->GetCacheBlockSize() == 1)
	  {
	    gf->GetElementVector (multidimcomponent, dnums, elu);
	  }
	else
	  {
	    FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
	    //gf->GetElementVector (multidimcomponent, dnums, elu2);
	    gf->GetElementVector (0, dnums, elu2);
	    for(int i=0; i<elu.Size(); i++)
	      elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
	  }

	fes.TransformVec (ei, elu, TRANSFORM_SOL);

	IntegrationPoint ip(lam1, lam2, lam3, 0);
	MappedIntegrationPoint<3,3> mip (ip, eltrans);

        for (int i = 0; i < components; i++) values[i] = 0;

        bool ok = false;
	for(int j = 0; j < bfi3d.Size(); j++)
          if (bfi3d[j]->DefinedOn(ma->GetElIndex(ei)))
            {
              HeapReset hr(lh);
              FlatVector<SCAL> flux(bfi3d[j] -> DimFlux(), lh);
              bfi3d[j]->CalcFlux (fel, mip, elu, flux, applyd, lh);
              
              for (int i = 0; i < components; i++)
                values[i] += ((double*)(void*)&flux(0))[i];
              ok = true;
            }

        return ok; 
      }
    
    catch (Exception & e)
      {
        cout << "GetValue caught exception" << endl
             << e.What();
        return false;
      }
    catch (exception & e)
      {
        cout << "GetValue caught exception" << endl
             << typeid(e).name() << endl;
        return false;
      }
  }


  template <class SCAL>
  bool VisualizeGridFunction<SCAL> :: 
  GetValue (int elnr, 
            const double xref[], const double x[], const double dxdxref[],
            double * values) 
  {
    // static Timer t("visgf::GetValue2");
    // RegionTimer reg(t);
    
    try
      {
	LocalHeapMem<100000> lh("visgf::getvalue");
        
	if (!bfi3d.Size()) return 0;
	if (gf -> GetLevelUpdated() < ma->GetNLevels()) return 0;
	
	const FESpace & fes = *gf->GetFESpace();
	
	int dim     = fes.GetDimension();
        ElementId ei(VOL,elnr);
	
	if ( !fes.DefinedOn(ei) ) return 0;
	
	const FiniteElement * fel = &fes.GetFE (ei, lh);

	Array<int> dnums(fel->GetNDof(), lh);
	fes.GetDofNrs (ei, dnums);

	FlatVector<SCAL> elu (fel->GetNDof() * dim, lh);

	if(gf->GetCacheBlockSize() == 1)
	  {
	    gf->GetElementVector (multidimcomponent, dnums, elu);
	  }
	else
	  {
	    FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
	    //gf->GetElementVector (multidimcomponent, dnums, elu2);
	    gf->GetElementVector (0, dnums, elu2);
	    for(int i=0; i<elu.Size(); i++)
	      elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
	  }
	fes.TransformVec (ei, elu, TRANSFORM_SOL);
	
	
	HeapReset hr(lh);
	
	Vec<3> vx;
	Mat<3,3> mdxdxref;
	for (int i = 0; i < 3; i++)
	  {
	    vx(i) = x[i];
	    for (int j = 0; j < 3; j++)
	      mdxdxref(i,j) = dxdxref[3*i+j];
	  }
	
	ElementTransformation & eltrans = ma->GetTrafo (ElementId(VOL, elnr), lh);
	IntegrationPoint ip(xref[0], xref[1], xref[2], 0);
	MappedIntegrationPoint<3,3> sip (ip, eltrans, vx, mdxdxref);
	
        for (int i = 0; i < components; i++) values[i] = 0;
        bool ok = false;

	for(int j = 0; j < bfi3d.Size(); j++)
          if (bfi3d[j]->DefinedOn(ma->GetElIndex(ei)))
            {
              FlatVector<SCAL> flux (bfi3d[j]->DimFlux(), lh);
              bfi3d[j]->CalcFlux (*fel, sip, elu, flux, applyd, lh); 
              
              for (int i = 0; i < components; i++)
                values[i] += ((double*)(void*)&flux(0))[i];
              ok = true;
            }
	
	return ok; 
      }
    catch (Exception & e)
      {
        cout << "GetValue 2 caught exception" << endl
             << e.What();
        return false;
      }
    catch (exception & e)
      {
        cout << "GetValue 2 caught exception" << endl
             << typeid(e).name() << endl;
        return false;
      }
  }



  template <class SCAL>
  bool VisualizeGridFunction<SCAL> ::
  GetMultiValue (int elnr, int facetnr, int npts,
		 const double * xref, int sxref,
		 const double * x, int sx,
		 const double * dxdxref, int sdxdxref,
		 double * values, int svalues)
  {
    if (npts > 100)
      {
        bool isdefined = false;
        for (int i = 0; i < npts; i += 100)
          {
            int npi = min2 (100, npts-i);
            isdefined = GetMultiValue (elnr, facetnr, npi, 
                                       xref+i*sxref, sxref, x+i*sx, sx, dxdxref+i*sdxdxref, sdxdxref,
                                       values+i*svalues, svalues);
          }
        return isdefined;
      }
    // static Timer t("visgf::GetMultiValue");
    // RegionTimer reg(t);

    try
      {
        if (!bfi3d.Size()) return 0;
        if (gf -> GetLevelUpdated() < ma->GetNLevels()) return 0;

        const FESpace & fes = *gf->GetFESpace();
        int dim = fes.GetDimension();
        
        ElementId ei(VOL,elnr);
        if (!fes.DefinedOn(ei)) return 0;

	
        LocalHeapMem<100000> lh("visgf::GetMultiValue");

	const ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
        const FiniteElement & fel = fes.GetFE (ei, lh);


	ArrayMem<int,200> dnums(fel.GetNDof());
	VectorMem<200,SCAL> elu (fel.GetNDof() * dim);

	fes.GetDofNrs (ei, dnums);

        if(gf->GetCacheBlockSize() == 1)
          {
            gf->GetElementVector (multidimcomponent, dnums, elu);
          }
        else
          {
            FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
            gf->GetElementVector (0, dnums, elu2);
            for(int i=0; i<elu.Size(); i++)
              elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
          }
        
        fes.TransformVec (ei, elu, TRANSFORM_SOL);
        
	if (!fes.DefinedOn(VOL, eltrans.GetElementIndex()))return 0;

	IntegrationRule ir; 
	ir.SetAllocSize(npts);
	for (int i = 0; i < npts; i++)
	  ir.Append (IntegrationPoint (xref[i*sxref], xref[i*sxref+1], xref[i*sxref+2]));

	MappedIntegrationRule<3,3> mir(ir, eltrans, 1, lh);

	for (int k = 0; k < npts; k++)
	  {
	    Mat<3,3> & mdxdxref = *new((double*)(dxdxref+k*sdxdxref)) Mat<3,3>;
	    FlatVec<3> vx((double*)x + k*sx);
	    mir[k] = MappedIntegrationPoint<3,3> (ir[k], eltrans, vx, mdxdxref);
	  }

	for (int k = 0; k < npts; k++)
	  for (int i = 0; i < components; i++)
	    values[k*svalues+i] = 0.0;

        bool isdefined = false;
	for(int j = 0; j < bfi3d.Size(); j++)
	  {
            if (!bfi3d[j]->DefinedOn(eltrans.GetElementIndex())) continue;
            isdefined = true;

	    HeapReset hr(lh);
            
	    FlatMatrix<SCAL> flux(npts, bfi3d[j]->DimFlux(), lh);
	    bfi3d[j]->CalcFlux (fel, mir, elu, flux, applyd, lh);
	    for (int k = 0; k < npts; k++)
	      for (int i = 0; i < components; i++)
		values[k*svalues+i] += ((double*)(void*)&flux(k,0))[i];
          }

        return isdefined;
      }
    catch (Exception & e)
      {
        cout << "GetMultiValue caught exception" << endl
             << e.What();
        return 0;
      }
    catch (exception & e)
      {
        cout << "GetMultiValue caught exception" << endl
             << typeid(e).name() << endl;
        return false;
      }
  }


















  template <class SCAL>
  bool VisualizeGridFunction<SCAL> :: GetSurfValue (int elnr, int facetnr, 
                                                    double lam1, double lam2, 
                                                    double * values) 
  { 
    // static Timer t("visgf::GetSurfValue");
    // RegionTimer reg(t);


    try
      {
	if (!bfi2d.Size()) return 0;
	if (gf -> GetLevelUpdated() < ma->GetNLevels()) return 0;

	VorB vb = (ma->GetDimension() == 3) ? BND : VOL;
        ElementId ei(vb, elnr);
	const FESpace & fes = *gf->GetFESpace();

	if (!fes.DefinedOn (ei))
	  return 0;

	int dim = fes.GetDimension();

	LocalHeapMem<100000> lh("VisGF::GetSurfValue");
	const FiniteElement & fel = fes.GetFE (ei, lh);

	ArrayMem<int, 100> dnums;
	fes.GetDofNrs (ei, dnums);

	FlatVector<SCAL> elu (dnums.Size() * dim, lh);

	if(gf->GetCacheBlockSize() == 1)
	  {
	    gf->GetElementVector (multidimcomponent, dnums, elu);
	  }
	else
	  {
	    FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
	    //gf->GetElementVector (multidimcomponent, dnums, elu2);
	    gf->GetElementVector (0, dnums, elu2);
	    for(int i=0; i<elu.Size(); i++)
	      elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
	  }

	fes.TransformVec (ei, elu, TRANSFORM_SOL);

	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
	if (!fes.DefinedOn(vb, eltrans.GetElementIndex())) return false;

	IntegrationPoint ip(lam1, lam2, 0, 0);
	ip.FacetNr() = facetnr;

	BaseMappedIntegrationPoint & mip = eltrans(ip, lh);
	for(int j = 0; j < bfi2d.Size(); j++)
	  {
	    FlatVector<SCAL> flux(bfi2d[j]->DimFlux(), lh);
	    bfi2d[j]->CalcFlux (fel, mip, elu, flux, applyd, lh);
	
	    for (int i = 0; i < components; i++)
	      {
		if(j == 0) values[i] = 0;
		values[i] += ((double*)(void*)&flux(0))[i];
	      }
	  }

	return true;
      }
    catch (Exception & e)
      {
        cout << "GetSurfaceValue caught exception" << endl
             << e.What();

        return 0;
      }
      
  }




  template <class SCAL>
  bool VisualizeGridFunction<SCAL> :: 
  GetSurfValue (int elnr, int facetnr, 
		const double xref[], const double x[], const double dxdxref[],
		double * values) 
  { 
    // static Timer t("visgf::GetSurfValue 2");
    // RegionTimer reg(t);

    try
      {
        if (!bfi2d.Size()) return 0;
        if (gf -> GetLevelUpdated() < ma->GetNLevels()) return 0;

        VorB vb = (ma->GetDimension() == 3) ? BND : VOL;
        ElementId ei(vb, elnr);

        const FESpace & fes = *gf->GetFESpace();

        int dim     = fes.GetDimension();

	// lh.CleanUp();
        LocalHeapMem<100000> lh("VisGF::GetSurfValue");
	const FiniteElement * fel = &fes.GetFE (ei, lh);
	
	Array<int> dnums(fel->GetNDof(), lh);
	FlatVector<SCAL> elu (fel->GetNDof()*dim, lh);

	fes.GetDofNrs (ei, dnums);
	
	if(gf->GetCacheBlockSize() == 1)
	  {
	    gf->GetElementVector (multidimcomponent, dnums, elu);
	  }
	else
	  {
	    FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
	    //gf->GetElementVector (multidimcomponent, dnums, elu2);
	    gf->GetElementVector (0, dnums, elu2);
	    for(int i=0; i<elu.Size(); i++)
	      elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
	  }
	
	fes.TransformVec (ei, elu, TRANSFORM_SOL);
	
	HeapReset hr(lh);
	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);
        if (!fes.DefinedOn(vb, eltrans.GetElementIndex())) return false;

        IntegrationPoint ip(xref[0], xref[1], 0, 0);
	ip.FacetNr() = facetnr;
        if (vb==BND)
          {
            // Vec<3> vx;
            Mat<3,2> mdxdxref;
            for (int i = 0; i < 3; i++)
              {
                // vx(i) = x[i];
                for (int j = 0; j < 2; j++)
                  mdxdxref(i,j) = dxdxref[2*i+j];
              }
            MappedIntegrationPoint<2,3> mip (ip, eltrans, (double*)x, mdxdxref); 
            for (int i = 0; i < components; i++)
              values[i] = 0.0;
            for(int j = 0; j<bfi2d.Size(); j++)
              {
		FlatVector<SCAL> flux(bfi2d[j]->DimFlux(), lh);
                bfi2d[j]->CalcFlux (*fel, mip, elu, flux, applyd, lh);
                for (int i = 0; i < components; i++)
                  values[i] += ((double*)(void*)&flux(0))[i];
              }
          }
        else
          {
            // Vec<2> vx;
            Mat<2,2> mdxdxref;
            for (int i = 0; i < 2; i++)
              {
                // vx(i) = x[i];
                for (int j = 0; j < 2; j++)
                  mdxdxref(i,j) = dxdxref[2*i+j];
              }
            MappedIntegrationPoint<2,2> mip (ip, eltrans, (double*)x, mdxdxref); 

            for (int i = 0; i < components; i++)
              values[i] = 0.0;
            for(int j = 0; j<bfi2d.Size(); j++)
              {
                FlatVector<SCAL> flux(bfi2d[j]->DimFlux(), lh);
                bfi2d[j]->CalcFlux (*fel, mip, elu, flux, applyd, lh);
                for (int i = 0; i < components; i++)
                  values[i] += ((double*)(void*)&flux(0))[i];
              }
          }

        return 1; 
      }
    catch (Exception & e)
      {
        cout << "GetSurfaceValue2 caught exception" << endl
             << e.What();

        return 0;
      }
      
  }


#ifdef __AVX__
  template <class SCAL>
  bool VisualizeGridFunction<SCAL> ::
  GetMultiSurfValue (size_t selnr, size_t facetnr, size_t npts,
                     const __m256d * xref, 
                     const __m256d * x, 
                     const __m256d * dxdxref, 
                     __m256d * values)
  {
    cout << "GetMultiSurf - gf not implemented" << endl;
    return false;
  }
#endif



  template <class SCAL>
  bool VisualizeGridFunction<SCAL> ::
  GetMultiSurfValue (int elnr, int facetnr, int npts,
                     const double * xref, int sxref,
                     const double * x, int sx,
                     const double * dxdxref, int sdxdxref,
                     double * values, int svalues)
  {
    if (npts > 100)
      {
        bool isdefined = false;
        for (int i = 0; i < npts; i += 100)
          {
            int npi = min2 (100, npts-i);
            isdefined = GetMultiSurfValue (elnr, facetnr, npi, 
                                           xref+i*sxref, sxref, x+i*sx, sx, dxdxref+i*sdxdxref, sdxdxref,
                                           values+i*svalues, svalues);
          }
        return isdefined;
      }

    // static Timer t("visgf::GetMultiSurfValue");
    // RegionTimer reg(t);

    try
      {
        if (!bfi2d.Size()) return 0;
        if (gf -> GetLevelUpdated() < ma->GetNLevels()) return 0;

        VorB vb = (ma->GetDimension() == 3) ? BND : VOL;
        ElementId ei(vb, elnr);
        
        const FESpace & fes = *gf->GetFESpace();
        int dim = fes.GetDimension();
        if (!fes.DefinedOn (ei))
          return 0;
        
        
        LocalHeapMem<100000> lh("visgf::getmultisurfvalue");

	ElementTransformation & eltrans = ma->GetTrafo (ei, lh);

        ArrayMem<int, 100> dnums;
	fes.GetDofNrs (ei, dnums);
	const FiniteElement & fel = fes.GetFE (ei, lh);

        FlatVector<SCAL> elu(dnums.Size() * dim, lh);

        if(gf->GetCacheBlockSize() == 1)
          {
            gf->GetElementVector (multidimcomponent, dnums, elu);
          }
        else
          {
            FlatVector<SCAL> elu2(dnums.Size()*dim*gf->GetCacheBlockSize(),lh);
            gf->GetElementVector (0, dnums, elu2);
            for(int i=0; i<elu.Size(); i++)
              elu[i] = elu2[i*gf->GetCacheBlockSize()+multidimcomponent];
          }
        
        fes.TransformVec (ei, elu, TRANSFORM_SOL);

        if (!fes.DefinedOn(vb, eltrans.GetElementIndex())) return false;
        
	SliceMatrix<> mvalues(npts, components, svalues, values);
	mvalues = 0;

	IntegrationRule ir(npts, lh);
	for (int i = 0; i < npts; i++)
	  {
	    ir[i] = IntegrationPoint (xref[i*sxref], xref[i*sxref+1]);
	    ir[i].FacetNr() = facetnr;
	  }
        
        if (vb==BND)
          {
	    MappedIntegrationRule<2,3> mir(ir, eltrans, 1, lh);

	    for (int k = 0; k < npts; k++)
	      {
		Mat<3,2> & mdxdxref = *new((double*)(dxdxref+k*sdxdxref)) Mat<3,2>;
		FlatVec<3> vx( (double*)x + k*sx);
		mir[k] = MappedIntegrationPoint<2,3> (ir[k], eltrans, vx, mdxdxref);
	      }

            bool isdefined = false;
	    for(int j = 0; j < bfi2d.Size(); j++)
	      {
                if (!bfi2d[j]->DefinedOn(eltrans.GetElementIndex())) continue;
                isdefined = true;

		FlatMatrix<SCAL> flux(npts, bfi2d[j]->DimFlux(), lh);
		bfi2d[j]->CalcFlux (fel, mir, elu, flux, applyd, lh);
		for (int k = 0; k < npts; k++)
		  mvalues.Row(k) += FlatVector<> (components, &flux(k,0));
	      }
            return isdefined;
          }
        else
          {
            if (!ma->GetDeformation())
              {
                MappedIntegrationRule<2,2> mir(ir, eltrans, 1, lh);
                
                for (int k = 0; k < npts; k++)
                  {
                    Mat<2,2> & mdxdxref = *new((double*)(dxdxref+k*sdxdxref)) Mat<2,2>;
                    FlatVec<2> vx( (double*)x + k*sx);
                    mir[k] = MappedIntegrationPoint<2,2> (ir[k], eltrans, vx, mdxdxref);
                  }

                bool isdefined = false;
                for(int j = 0; j < bfi2d.Size(); j++)
                  {
                    if (!bfi2d[j]->DefinedOn(eltrans.GetElementIndex())) continue;
                    isdefined = true;
                    FlatMatrix<SCAL> flux(npts, bfi2d[j]->DimFlux(), lh);
                    bfi2d[j]->CalcFlux (fel, mir, elu, flux, applyd, lh);

                    for (int k = 0; k < npts; k++)
                      mvalues.Row(k) += FlatVector<> (components, &flux(k,0));
                  }
                return isdefined;
              }
            else
              {
                MappedIntegrationRule<2,2> mir(ir, eltrans, lh);
                
                bool isdefined = false;
                for(int j = 0; j < bfi2d.Size(); j++)
                  {
                    if (!bfi2d[j]->DefinedOn(eltrans.GetElementIndex())) continue;
                    isdefined = true;
                    
                    FlatMatrix<SCAL> flux(npts, bfi2d[j]->DimFlux(), lh);
                    bfi2d[j]->CalcFlux (fel, mir, elu, flux, applyd, lh);

                    for (int k = 0; k < npts; k++)
                      mvalues.Row(k) += FlatVector<> (components, &flux(k,0));
                  }
                return isdefined;
              }
          }
      }
    catch (Exception & e)
      {
        cout << "GetMultiSurfaceValue caught exception" << endl
             << e.What();

        return 0;
      }
  }




  template <class SCAL>
  bool VisualizeGridFunction<SCAL> ::
  GetSegmentValue (int segnr, double xref, double * values)
  {
    if (ma->GetDimension() != 1) return false;

    LocalHeapMem<100000> lh("visgf::getsegmentvalue");

    const FESpace & fes = *gf->GetFESpace();
    auto eval = fes.GetEvaluator (VOL);
    FlatVector<> fvvalues (eval->Dim(), values);
    ElementId ei (VOL, segnr);  // ???? VOL

    const FiniteElement & fel = fes.GetFE (ei, lh);
    Array<int> dnums(fel.GetNDof(), lh);
    fes.GetDofNrs (ei, dnums);

    FlatVector<> elvec(fes.GetDimension()*dnums.Size(), lh);
    gf->GetElementVector (dnums, elvec);
    
    const ElementTransformation & trafo = ma->GetTrafo (ei, lh);
    IntegrationPoint ip (xref);

    eval -> Apply (fel, trafo(ip, lh), elvec, fvvalues, lh);

    return true;
  }










  
  template <class SCAL>
  void VisualizeGridFunction<SCAL> :: 
  Analyze(Array<double> & minima, Array<double> & maxima, Array<double> & averages, int component)
  {
    try
      {
    // cout << "VisGF::Analyze" << endl;
    int ndomains = 0;

    if (bfi3d.Size()) 
      ndomains = ma->GetNDomains();
    else if(bfi2d.Size()) 
      ndomains = ma->GetNBoundaries();

    Array<double> volumes(ndomains);

    Analyze(minima,maxima,averages,volumes,component);
    
    for(int i=0; i<ndomains; i++)
      {
	if(component == -1)
	  for(int j=0; j<components; j++)
	    averages[i*components+j] /= volumes[i];
	else
	  averages[i] /= volumes[i];
      }
      }
    catch (Exception e)
      {
        cerr << "Caught exception in VisualizeGF::Analyze: " << e.What() << endl;
      }
  }
  

  template <class SCAL>
  void VisualizeGridFunction<SCAL> :: 
  Analyze(Array<double> & minima, Array<double> & maxima, Array<double> & averages_times_volumes, Array<double> & volumes, int component)
  {
    try
      {
    // cout << "VisGF::Analyze2" << endl;
    const FESpace & fes = *gf->GetFESpace();

    int domain;
    double *val;
    int pos;
    double vol;

    /*
      int ndomains;
      if(bfi3d.Size()) ndomains = ma->GetNDomains();
      else if(bfi2d.Size()) ndomains = ma->GetNBoundaries();
    */

    Array<double> posx;
    Array<double> posy;
    Array<double> posz;
    ELEMENT_TYPE cache_type = ET_SEGM;
	
    LocalHeapMem<100000> lh2("Gridfunction - Analyze");
	
    val = new double[components];
			

    for(int i = 0; i < minima.Size(); i++)
      {
	minima[i] = 1e100;
	maxima[i] = -1e100;
	averages_times_volumes[i] = 0;
      }

    for(int i=0; i<volumes.Size(); i++) volumes[i] = 0;
    
    void * heapp = lh2.GetPointer();
    if(bfi3d.Size())
      {
	for(int i=0; i<ma->GetNE(); i++)
	  {
            ElementId ei(VOL, i);
	    const FiniteElement & fel = fes.GetFE(ei,lh2);
	    
	    domain = ma->GetElIndex(ei);
	    
	    vol = ma->ElementVolume(i);
	    
	    volumes[domain] += vol;
	    
	    if(fel.ElementType() != cache_type)
	      {
		posx.DeleteAll(); posy.DeleteAll(); posz.DeleteAll(); 
		switch(fel.ElementType())
		  {
		  case ET_TET:
		    posx.Append(0.25); posy.Append(0.25); posz.Append(0.25); 
		    posx.Append(0); posy.Append(0); posz.Append(0); 
		    posx.Append(1); posy.Append(0); posz.Append(0);
		    posx.Append(0); posy.Append(1); posz.Append(0);
		    posx.Append(0); posy.Append(0); posz.Append(1);
		    break;
		  case ET_HEX:
		    posx.Append(0.5); posy.Append(0.5); posz.Append(0.5);  
		    posx.Append(0); posy.Append(0); posz.Append(0); 
		    posx.Append(0); posy.Append(0); posz.Append(1);
		    posx.Append(0); posy.Append(1); posz.Append(0);
		    posx.Append(0); posy.Append(1); posz.Append(1);  
		    posx.Append(1); posy.Append(0); posz.Append(0); 
		    posx.Append(1); posy.Append(0); posz.Append(1);
		    posx.Append(1); posy.Append(1); posz.Append(0);
		    posx.Append(1); posy.Append(1); posz.Append(1);
		    break;
                  default:
                    {
                      bool firsttime = 1;
                      if (firsttime)
                        {
                          cerr << "WARNING::VisGridFunction::Analyze: unsupported element "
                               << ElementTopology::GetElementName(fel.ElementType()) << endl;
                          firsttime = 0;
                        }
                      break;
                    } 
		  }
		cache_type = fel.ElementType();
	      }
	    
	    
	    for(int k=0; k<posx.Size(); k++)
	      {
		GetValue(i,posx[k],posy[k],posz[k],val);


		if(component == -1)
		  {
		    for(int j=0; j<components; j++)
		      {
			pos = domain*components+j;
			if(val[j] > maxima[pos]) maxima[pos] = val[j];
			if(val[j] < minima[pos]) minima[pos] = val[j];
			if(k==0) averages_times_volumes[pos] += val[j]*vol;
		      }
		  }
		else
		  {
		    pos = domain;
		    if(val[component] > maxima[pos]) maxima[pos] = val[component];
		    if(val[component] < minima[pos]) minima[pos] = val[component];
		    if(k==0) averages_times_volumes[pos] += val[component]*vol;
		  }
	      }
	    lh2.CleanUp(heapp);
	  }
      }
    else if (bfi2d.Size())
      {
	for(int i=0; i<ma->GetNSE(); i++)
	  {
            ElementId ei(BND, i);
	    const FiniteElement & fel = fes.GetFE(ei,lh2);

	    domain = ma->GetElIndex(ei);

	    vol = ma->SurfaceElementVolume(i);

	    volumes[domain] += vol;

	    if(fel.ElementType() != cache_type)
	      {
		posx.DeleteAll(); posy.DeleteAll(); posz.DeleteAll(); 
		switch(fel.ElementType())
		  {
		  case ET_POINT:  // ??
		  case ET_SEGM: 
		  case ET_TET: case ET_HEX: case ET_PRISM: case ET_PYRAMID:
		    break;
		    
		  case ET_TRIG:
		    posx.Append(0.33); posy.Append(0.33);
		    posx.Append(0); posy.Append(0);
		    posx.Append(0); posy.Append(1);
		    posx.Append(1); posy.Append(0);
		    break;
		  case ET_QUAD:
		    posx.Append(0.5); posy.Append(0.5);
		    posx.Append(0); posy.Append(0);
		    posx.Append(0); posy.Append(1); 
		    posx.Append(1); posy.Append(0);
		    posx.Append(1); posy.Append(1);
		    break;
		  }
		cache_type = fel.ElementType();
	      }
	    for(int k=0; k<posx.Size(); k++)
	      {
		GetSurfValue(i,-1, posx[k],posy[k],val);
		if(component == -1)
		  {
		    for(int j=0; j<components; j++)
		      {
			pos = domain*components+j;
			if(val[j] > maxima[pos]) maxima[pos] = val[j];
			if(val[j] < minima[pos]) minima[pos] = val[j];
			if(k==0) averages_times_volumes[pos] += val[j]*vol;
		      }
		  }
		else
		  {
		    pos = domain;
		    if(val[component] > maxima[pos]) maxima[pos] = val[component];
		    if(val[component] < minima[pos]) minima[pos] = val[component];
		    if(k==0) averages_times_volumes[pos] += val[component]*vol;
		  }
	      }
	    lh2.CleanUp(heapp);
	  }
      }

    delete [] val;
      }
    catch (Exception e)
      {
        cerr << "Caught exception in VisualizeGF::Analyze2: " << e.What() << endl;
      }
  }







  VisualizeCoefficientFunction :: 
  VisualizeCoefficientFunction (shared_ptr<MeshAccess> ama,
				shared_ptr<CoefficientFunction> acf)
    : SolutionData ("coef", acf->Dimension(), false /* complex */),
      ma(ama), cf(acf)
  { 
    ; // cout << "viscoef, con't" << endl;
  }
  
  VisualizeCoefficientFunction :: ~VisualizeCoefficientFunction ()
  {
    ; // cout << "viscoef, dest" << endl;
  }
  
  bool VisualizeCoefficientFunction :: GetValue (int elnr, 
						 double lam1, double lam2, double lam3,
						 double * values) 
  {
    try
      {
    // cout << "viscoef, getval1" << endl;
    LocalHeapMem<100000> lh("viscf::GetValue");
    IntegrationPoint ip(lam1, lam2, lam3);
    ElementId ei(VOL, elnr);
    ElementTransformation & trafo = ma->GetTrafo (ei, lh);
    BaseMappedIntegrationPoint & mip = trafo(ip, lh);
    if (!cf -> IsComplex())
      cf -> Evaluate (mip, FlatVector<>(GetComponents(), values));
    else
      cf -> Evaluate (mip, FlatVector<Complex>(GetComponents(), values));
    return true;

      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetValue caught exception: " << endl
             << e.What();
        return 0;
      }
      
    
  }
  
  bool VisualizeCoefficientFunction :: 
  GetValue (int elnr, 
	    const double xref[], const double x[], const double dxdxref[],
	    double * values) 
  {
    try
      {
    // cout << "viscoef, getval2" << endl;
    LocalHeapMem<100000> lh("viscf::GetValue xref");
    IntegrationPoint ip(xref[0],xref[1],xref[2]);
    ElementId ei(VOL, elnr);
    ElementTransformation & trafo = ma->GetTrafo (ei, lh);
    BaseMappedIntegrationPoint & mip = trafo(ip, lh);
    if (!cf -> IsComplex())
      cf -> Evaluate (mip, FlatVector<>(GetComponents(), values));
    else
      cf -> Evaluate (mip, FlatVector<Complex>(GetComponents(), values));      
    return true;

      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetValue caught exception: " << endl
             << e.What();
        return 0;
      }
      
    
  }

  bool VisualizeCoefficientFunction :: 
  GetMultiValue (int elnr, int facetnr, int npts,
		 const double * xref, int sxref,
		 const double * x, int sx,
		 const double * dxdxref, int sdxdxref,
		 double * values, int svalues)
  {
    if (npts > 128)
      {
        bool isdefined = false;
        for (int i = 0; i < npts; i += 128)
          {
            int npi = min2 (128, npts-i);
            isdefined = GetMultiValue (elnr, facetnr, npi, 
                                       xref+i*sxref, sxref, x+i*sx, sx, dxdxref+i*sdxdxref, sdxdxref,
                                       values+i*svalues, svalues);
          }
        return isdefined;
      }

    
    try
      {
        LocalHeapMem<100000> lh("viscf::GetMultiValue xref");

        IntegrationRule ir(npts, lh);
        for (size_t j = 0; j < npts; j++)
          ir[j] = IntegrationPoint(xref[j*sxref], xref[j*sxref+1], xref[j*sxref+2]);
        
        ElementId ei(VOL, elnr);
        ElementTransformation & trafo = ma->GetTrafo (ei, lh);
        BaseMappedIntegrationRule & mir = trafo(ir, lh);
        
        if (!cf -> IsComplex())
          cf -> Evaluate (mir, FlatMatrix<>(npts, GetComponents(), values));
        else
          cf -> Evaluate (mir, FlatMatrix<Complex>(npts, GetComponents(), reinterpret_cast<Complex*>(values)));      

        return true;
      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetMultiValue caught exception: " << endl
             << e.What();
        return 0;
      }
      
    
  }
  
  bool VisualizeCoefficientFunction ::  
  GetSurfValue (int elnr, int facetnr,
		double lam1, double lam2, 
		double * values) 
  {
    try
      {
    // cout << "viscoef, getsurfval1" << endl;
    LocalHeapMem<100000> lh("viscf::GetSurfValue");
    IntegrationPoint ip(lam1, lam2);
    ip.FacetNr() = facetnr;
    VorB vb = ma->GetDimension() == 3 ? BND : VOL;
    ElementId ei(vb, elnr);
    ElementTransformation & trafo = ma->GetTrafo (ei, lh);
    BaseMappedIntegrationPoint & mip = trafo(ip, lh);

    if (!cf -> IsComplex())
      cf -> Evaluate (mip, FlatVector<>(GetComponents(), values));
    else
      cf -> Evaluate (mip, FlatVector<Complex>(GetComponents(), values));

    return true;

      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetSurfValue caught exception: " << endl
             << e.What();
        return 0;
      }
  }

  bool VisualizeCoefficientFunction ::  GetSurfValue (int selnr, int facetnr, 
						      const double xref[], const double x[], const double dxdxref[],
						      double * values)
  {
    cout << "visualizecoef, getsurfvalue (xref) not implemented" << endl;
    return false;
  }


#ifdef __AVX__  
  bool VisualizeCoefficientFunction ::    
  GetMultiSurfValue (size_t selnr, size_t facetnr, size_t npts,
                     const __m256d * xref, 
                     const __m256d * x, 
                     const __m256d * dxdxref, 
                     __m256d * values)
  {
    try
      {
        /*   todo
        if (cf -> IsComplex())
          {
            for (int i = 0; i < npts; i++)
              GetSurfValue (selnr, facetnr, xref[i*sxref], xref[i*sxref+1], &values[i*svalues]);
            return true;
          }
        */
        
        bool bound = (ma->GetDimension() == 3);
        ElementId ei(bound ? BND : VOL, selnr);
        
        LocalHeapMem<1000000> lh("viscf::getmultisurfvalue");
        ElementTransformation & eltrans = ma->GetTrafo (ei, lh);

        FlatMatrix<SIMD<double>> mvalues(GetComponents(), npts, (SIMD<double>*)values);

        constexpr size_t BS = 64;
        for (size_t base = 0; base < npts; base +=BS)
          {
            size_t ni = min2(npts-base, BS);
            
            SIMD_IntegrationRule ir(SIMD<double>::Size()*ni, lh);
            for (size_t i = 0; i < ni; i++)
              {
                ir[i](0) = xref[2*(base+i)];
                ir[i](1) = xref[2*(base+i)+1];
                ir[i].FacetNr() = facetnr;
              }

            if (bound)
              {
                SIMD_MappedIntegrationRule<2,3> mir(ir, eltrans, 1, lh);
                for (size_t k = 0; k < ni; k++)
                  {
                    const Mat<2,3,SIMD<double>> & mdxdxref = reinterpret_cast<const Mat<2,3,SIMD<double>>&> (dxdxref[6*(k+base)]);
                    const Vec<3,SIMD<double>> & vx = reinterpret_cast<const Vec<3,SIMD<double>>&> (x[3*(k+base)]);
                    mir[k] = SIMD<MappedIntegrationPoint<2,3>> (ir[k], eltrans, vx, mdxdxref);
                  }
                
                cf -> Evaluate (mir, mvalues.Cols(base, base+ni));
              }
            else
              {
                if (!ma->GetDeformation())
                  {
                    SIMD_MappedIntegrationRule<2,2> mir(ir, eltrans, 1, lh);
                    
                    for (size_t k = 0; k < ni; k++)
                      {
                        auto & mdxdxref = reinterpret_cast<const Mat<2,2,SIMD<double>>&> (dxdxref[6*(k+base)]);
                        auto & vx = reinterpret_cast<const Vec<2,SIMD<double>>&> (x[3*(k+base)]);
                        mir[k] = SIMD<MappedIntegrationPoint<2,2>> (ir[k], eltrans, vx, mdxdxref);
                      }
                    
                    cf -> Evaluate (mir, mvalues.Cols(base, base+ni));
                  }
                else
                  {
                    SIMD_MappedIntegrationRule<2,2> mir(ir, eltrans, lh);
                    cf -> Evaluate (mir, mvalues.Cols(base, base+ni));
                  }
              }
          }
        return true;
      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetMultiSurfValue caught exception (AVX): " << endl
             << e.What();
        return 0;
      }
  }
#endif
  
  
  bool VisualizeCoefficientFunction ::  
  GetMultiSurfValue (int selnr, int facetnr, int npts,
		     const double * xref, int sxref,
		     const double * x, int sx,
		     const double * dxdxref, int sdxdxref,
		     double * values, int svalues)
  {
    if (npts > 100)
      {
        bool isdefined = false;
        for (int i = 0; i < npts; i += 100)
          {
            int npi = min2 (100, npts-i);
            isdefined = GetMultiSurfValue (selnr, facetnr, npi, 
                                           xref+i*sxref, sxref, x+i*sx, sx, dxdxref+i*sdxdxref, sdxdxref,
                                           values+i*svalues, svalues);
          }
        return isdefined;
      }

    try
      {
    
    
        // static Timer t("VisualizeCoefficientFunction::GetMultiSurfValue", 2); RegionTimer reg(t);
        // static Timer t2("VisualizeCoefficientFunction::GetMultiSurfValue evaluate", 2);

    if (cf -> IsComplex())
      {
        for (int i = 0; i < npts; i++)
          GetSurfValue (selnr, facetnr, xref[i*sxref], xref[i*sxref+1], &values[i*svalues]);
        return true;
      }
    
    VorB vb = (ma->GetDimension() == 3) ? BND : VOL;
    ElementId ei(vb, selnr);
        
    LocalHeapMem<100000> lh("viscf::getmultisurfvalue");
    ElementTransformation & eltrans = ma->GetTrafo (ei, lh);

    FlatMatrix<> mvalues1(npts, GetComponents(), lh);

    IntegrationRule ir(npts, lh);
    for (int i = 0; i < npts; i++)
      {
        ir[i] = IntegrationPoint (xref[i*sxref], xref[i*sxref+1]);
        ir[i].FacetNr() = facetnr;
      }
        
    if (vb==BND)
      {
        MappedIntegrationRule<2,3> mir(ir, eltrans, 1, lh);

        for (int k = 0; k < npts; k++)
          {
            Mat<3,2> & mdxdxref = *new((double*)(dxdxref+k*sdxdxref)) Mat<3,2>;
            FlatVec<3> vx( (double*)x + k*sx);
            mir[k] = MappedIntegrationPoint<2,3> (ir[k], eltrans, vx, mdxdxref);
          }

        // RegionTimer r2(t2);
        cf -> Evaluate (mir, mvalues1);
      }
    else
      {
        if (!ma->GetDeformation())
          {
            MappedIntegrationRule<2,2> mir(ir, eltrans, 1, lh);
            
            for (int k = 0; k < npts; k++)
              {
                Mat<2,2> & mdxdxref = *new((double*)(dxdxref+k*sdxdxref)) Mat<2,2>;
                FlatVec<2> vx( (double*)x + k*sx);
                mir[k] = MappedIntegrationPoint<2,2> (ir[k], eltrans, vx, mdxdxref);
              }
            // RegionTimer r2(t2);            
            cf -> Evaluate (mir, mvalues1);
          }
        else
          {
            MappedIntegrationRule<2,2> mir(ir, eltrans, lh);
            cf -> Evaluate (mir, mvalues1);
          }
      }

    SliceMatrix<> mvalues(npts, GetComponents(), svalues, values);
    mvalues = mvalues1;
    return true;

      }
    catch (Exception & e)
      {
        cout << "VisualizeCoefficientFunction::GetMultiSurfValue caught exception: " << endl
             << e.What();
        return 0;
      }
      
    
  }


  void VisualizeCoefficientFunction ::  
  Analyze(Array<double> & minima, Array<double> & maxima, Array<double> & averages, 
	  int component)
  {
    cout << "visualizecoef, analyze1 not implemented" << endl;
  }

  void VisualizeCoefficientFunction :: 
  Analyze(Array<double> & minima, Array<double> & maxima, Array<double> & averages_times_volumes, 
	  Array<double> & volumes, int component)
  {
    cout << "visualizecoef, analyzed2 not implemented" << endl;
  }






  template class T_GridFunction<double>;
  template class T_GridFunction<Vec<2> >;
  template class T_GridFunction<Vec<3> >;
  template class T_GridFunction<Vec<4> >;
  

  template class  VisualizeGridFunction<double>;
  template class  VisualizeGridFunction<Complex>;

}


