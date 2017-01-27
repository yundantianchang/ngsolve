#ifdef NGS_PYTHON
#include "../ngstd/python_ngstd.hpp"
#include <comp.hpp>

#ifdef PARALLEL
#include <mpi4py/mpi4py.h>
#endif

#include <regex>

//#define COMPILE_DOCU


using namespace ngcomp;

using ngfem::ELEMENT_TYPE;

typedef GridFunction GF;
typedef PyWrapper<GF> PyGF;
typedef PyWrapper<FESpace> PyFES;
typedef PyWrapper<BaseVector> PyBaseVector;
typedef PyWrapper<BaseMatrix> PyBaseMatrix;

// template <typename T>
// struct PythonTupleFromFlatArray {
//   static PyObject* convert(FlatArray<T> ar)
//     {
//       py::list res;
//       for(int i = 0; i < ar.Size(); i++) 
//         res.append (ar[i]);
//       py::tuple tup(res);
//       return py::incref(tup.ptr());
//     }
// };
// 
// template <typename T>
// struct PythonTupleFromArray {
//   static PyObject* convert(const Array<T> & ar)
//     {
//       py::list res;
//       for(int i = 0; i < ar.Size(); i++) 
//         res.append (ar[i]);
//       py::tuple tup(res);
//       return py::incref(tup.ptr());
//     }
// };
// 
// 
// template <typename T> void PyExportArray ()
// {
//   boost::python::to_python_converter< FlatArray<T>, PythonTupleFromFlatArray<T> >();
//   boost::python::to_python_converter< Array<T>, PythonTupleFromArray<T> >();
// }
// 



template <> class cl_NonElement<ElementId>
{
public:
  static ElementId Val() { return ElementId(VOL,-1); }
};


class PyNumProc : public NumProc
{

public:
  PyNumProc (shared_ptr<PDE> pde, const Flags & flags) : NumProc (pde, flags) { ; }
  shared_ptr<PDE> GetPDE() const { return shared_ptr<PDE> (pde); }
  // virtual void Do (LocalHeap & lh) { cout << "should not be called" << endl; }
};

// class NumProcWrap : public PyNumProc, public py::wrapper<PyNumProc> {
// public:
//   NumProcWrap (shared_ptr<PDE> pde, const Flags & flags) : PyNumProc(pde, flags) { ; }
//   virtual void Do(LocalHeap & lh)  {
//     // cout << "numproc wrap - do" << endl;
//     AcquireGIL gil_lock;
//     try
//       {
//         this->get_override("Do")(boost::ref(lh));
//       }
//     catch (py::error_already_set const &) {
//       cout << "caught a python error:" << endl;
//       PyErr_Print();
//     }
//   }
// };

typedef PyWrapperDerived<ProxyFunction, CoefficientFunction> PyProxyFunction;

py::object MakeProxyFunction2 (const FESpace & fes,
                              bool testfunction,
                              const function<shared_ptr<ProxyFunction>(shared_ptr<ProxyFunction>)> & addblock)
{
  auto compspace = dynamic_cast<const CompoundFESpace*> (&fes);
  if (compspace && !fes.GetEvaluator())
    {
      py::list l;
      int nspace = compspace->GetNSpaces();
      for (int i = 0; i < nspace; i++)
        {
          l.append (MakeProxyFunction2 ( *(*compspace)[i], testfunction,
                                         [&] (shared_ptr<ProxyFunction> proxy)
                                         {
                                           auto block_eval = make_shared<CompoundDifferentialOperator> (proxy->Evaluator(), i);
                                           shared_ptr <CompoundDifferentialOperator> block_deriv_eval = nullptr;
                                           if (proxy->DerivEvaluator() != nullptr)
                                             block_deriv_eval = make_shared<CompoundDifferentialOperator> (proxy->DerivEvaluator(), i);
                                           shared_ptr <CompoundDifferentialOperator> block_trace_eval = nullptr;
                                           if (proxy->TraceEvaluator() != nullptr)
                                             block_trace_eval = make_shared<CompoundDifferentialOperator> (proxy->TraceEvaluator(), i);
					   shared_ptr <CompoundDifferentialOperator> block_ttrace_eval = nullptr;
					   if (proxy->TTraceEvaluator() != nullptr)
					     block_ttrace_eval = make_shared<CompoundDifferentialOperator> (proxy->TTraceEvaluator(),i);
                                           shared_ptr <CompoundDifferentialOperator> block_trace_deriv_eval = nullptr;
                                           if (proxy->TraceDerivEvaluator() != nullptr)
                                             block_trace_deriv_eval = make_shared<CompoundDifferentialOperator> (proxy->TraceDerivEvaluator(), i);
					   shared_ptr <CompoundDifferentialOperator> block_ttrace_deriv_eval = nullptr;
					   if (proxy->TTraceDerivEvaluator() != nullptr)
					     block_ttrace_deriv_eval = make_shared<CompoundDifferentialOperator> (proxy->TTraceDerivEvaluator(),i);
                                           auto block_proxy = make_shared<ProxyFunction> (/* &fes, */ testfunction, fes.IsComplex(),                                                                                          block_eval, block_deriv_eval, block_trace_eval, block_trace_deriv_eval,
					  block_ttrace_eval, block_ttrace_deriv_eval);

                                           SymbolTable<shared_ptr<DifferentialOperator>> add = proxy->GetAdditionalEvaluators();
                                           for (int j = 0; j < add.Size(); j++)
                                             block_proxy->SetAdditionalEvaluator(add.GetName(j),
                                                                                 make_shared<CompoundDifferentialOperator> (add[j], i));
                                           
                                           block_proxy = addblock(block_proxy);
                                           return block_proxy;
                                         }));
        }
      return l;
    }

  /*
  shared_ptr<CoefficientFunction> proxy =
    addblock(make_shared<ProxyFunction> (testfunction, fes.IsComplex(),
                                         fes.GetEvaluator(),
                                         fes.GetFluxEvaluator(),
                                         fes.GetEvaluator(BND),
                                         fes.GetFluxEvaluator(BND)
                                         ));
  */
  auto proxy = make_shared<ProxyFunction>  (testfunction, fes.IsComplex(),
                                            fes.GetEvaluator(),
                                            fes.GetFluxEvaluator(),
                                            fes.GetEvaluator(BND),
                                            fes.GetFluxEvaluator(BND),
					    fes.GetEvaluator(BBND),
					    fes.GetFluxEvaluator(BBND));
  auto add_diffops = fes.GetAdditionalEvaluators();
  for (int i = 0; i < add_diffops.Size(); i++)
    proxy->SetAdditionalEvaluator (add_diffops.GetName(i), add_diffops[i]);

  proxy = addblock(proxy);
  return py::cast(PyProxyFunction(proxy));
}

py::object MakeProxyFunction (const FESpace & fes,
                              bool testfunction) 
{
  return 
    MakeProxyFunction2 (fes, testfunction, 
                        [&] (shared_ptr<ProxyFunction> proxy) { return proxy; });
}








class GlobalDummyVariables 
{
public:
  int GetMsgLevel() { return printmessage_importance; }
  void SetMsgLevel(int msg_level) 
  {
    // cout << "set printmessage_importance to " << msg_level << endl;
    printmessage_importance = msg_level; 
    netgen::printmessage_importance = msg_level; 
  }
  string GetTestoutFile () const
  {
    return "no-filename-here";
  }
  void SetTestoutFile(string filename) 
  {
    // cout << "set testout-file to " << filename << endl;
    testout = new ofstream(filename);
  }
  
};
static GlobalDummyVariables globvar;

typedef PyWrapper<CoefficientFunction> PyCF;
typedef PyWrapper<PML_Transformation> PyPML;
typedef PyWrapperDerived<RadialPML_Transformation<0>,PML_Transformation> PyRadPML; 
typedef PyWrapperDerived<CustomPML_Transformation<0>,PML_Transformation> PyCustPML; 
typedef PyWrapperDerived<CartesianPML_Transformation<0>,PML_Transformation> PyCartPML; 
typedef PyWrapperDerived<BrickRadialPML_Transformation<0>,PML_Transformation> PyBrickPML; 
void ExportPml(py::module &m)
{
  py::class_<PyPML>(m, "PML", "Base pml object")
    .def("PrintParameters", [](PyPML *instance){ instance->Get()->PrintParameters();})
    .def("__call__",  [](PyPML *instance, py::tuple _point) {
                      int dim = py::len(_point);
                      PyPML dimpml = instance->Get()->CreateDim(dim);
                      Vector<double> hpoint(dim);
                      Vector<Complex> point(dim);
                      for (int i : Range(dim))
                        hpoint[i] = py::extract<double>(_point[i])();
                      Matrix<Complex> jac(dim,dim);
                      dimpml.Get()->MapPointV(hpoint,point,jac);
                      return point;
                    },"map a point")
    .def("__call__",  [](PyPML *instance, double x) {
                      PyPML dimpml = instance->Get()->CreateDim(1);
                      Vector<double> hpoint(1);
                      hpoint[0]=x;
                      Vector<Complex> point(1);
                      Matrix<Complex> jac(1,1);
                      dimpml.Get()->MapPointV(hpoint,point,jac);
                      return point;
                    },"map a point")
    .def("__call__",  [](PyPML *instance, const BaseMappedIntegrationPoint & _point) {
                      int dim = _point.Dim();
                      PyPML dimpml = instance->Get()->CreateDim(dim);
                      Vector<Complex> point(dim);
                      Matrix<Complex> jac(dim,dim);
                      dimpml.Get()->MapPointV(_point,point,jac);
                      return point;
                    },"map a point")

    .def("jac",  [](PyPML *instance, py::tuple _point) {
                      int dim = py::len(_point);
                      PyPML dimpml = instance->Get()->CreateDim(dim);
                      Vector<double> hpoint(dim);
                      Vector<Complex> point(dim);
                      for (int i : Range(dim))
                        hpoint(i) = py::extract<double>(_point[i])();
                      Matrix<Complex> jac(dim,dim);
                      dimpml.Get()->MapPointV(hpoint,point,jac);
                      return jac; 
                    },
                    "jacobian at point")
    .def("jac",  [](PyPML *instance, double x) {
                      PyPML dimpml = instance->Get()->CreateDim(1);
                      Vector<double> hpoint(1);
                      hpoint[0]=x;
                      Vector<Complex> point(1);
                      Matrix<Complex> jac(1,1);
                      dimpml.Get()->MapPointV(hpoint,point,jac);
                      return jac;
                    },"jacobian at point")
    .def("jac",  [](PyPML *instance, const BaseMappedIntegrationPoint & _point) {
                      int dim = _point.Dim();
                      PyPML dimpml = instance->Get()->CreateDim(dim);
                      Vector<Complex> point(dim);
                      Matrix<Complex> jac(dim,dim);
                      dimpml.Get()->MapPointV(_point,point,jac);
                      return jac; 
                    },
                    "jacobian at point")
  ;

  py::class_<PyRadPML,PyPML>(m, "Radial", "radial pml scaling")
    .def("__init__", [](PyRadPML *instance, double rad, Complex alpha) {
        auto pml = make_shared<RadialPML_Transformation<0>> (rad,alpha);
        new (instance) PyRadPML(pml);
        },
        py::arg("rad")=1, py::arg("alpha")=Complex(0,1))
    
    ;

  py::class_<PyCustPML,PyPML>(m, "Custom", "pml with custom transformation")
    .def("__init__", [](PyCustPML *instance, PyCF trafo, PyCF jac) {
        auto pml = make_shared<CustomPML_Transformation<0>> (trafo.Get(),jac.Get());
        new (instance) PyCustPML(pml);
        },
        py::arg("trafo"),py::arg("jac"))
    
    ;

  py::class_<PyCartPML,PyPML>(m, "Cartesian", "cartesian pml scaling")
    .def("__init__", [](PyCartPML *instance, py::tuple mins,py::tuple maxs, Complex alpha) {
          Matrix<double> bounds = 0;
          bounds.SetSize(min(py::len(mins),py::len(maxs)),2);
          for (int j :Range(bounds.Height()))
            {
              bounds(j,0)=py::extract<double>(mins[j])();
              bounds(j,1)=py::extract<double>(maxs[j])();
            }
        new (instance) PyCartPML(make_shared<CartesianPML_Transformation<0>>(bounds,alpha));
        },
        py::arg("mins"),py::arg("maxs"), py::arg("alpha")=Complex(0,1))
    ;
  
  py::class_<PyBrickPML,PyPML>(m, "BrickRadial", "generalized radial pml scaling on a brick")
    .def("__init__", [](PyBrickPML *instance, py::tuple mins,py::tuple maxs, Complex alpha) {
          Matrix<double> bounds = 0;
          bounds.SetSize(min(py::len(mins),py::len(maxs)),2);
          for (int j :Range(bounds.Height()))
            {
              bounds(j,0)=py::extract<double>(mins[j])();
              bounds(j,1)=py::extract<double>(maxs[j])();
            }
        new (instance) PyPML(make_shared<BrickRadialPML_Transformation<0>>(bounds,alpha));
        },
        py::arg("mins"),py::arg("maxs"), py::arg("alpha")=Complex(0,1))
    ;
}



void NGS_DLL_HEADER ExportNgcomp(py::module &m)
{

  py::module pml = m.def_submodule("pml", "module for perfectly matched layers");
  ExportPml(pml);
  //////////////////////////////////////////////////////////////////////////////////////////

  py::enum_<VorB>(m, "VorB")
    .value("VOL", VOL)
    .value("BND", BND)
    .value("BBND", BBND)
    .export_values()
    ;

  //////////////////////////////////////////////////////////////////////////////////////////

  py::enum_<COUPLING_TYPE> (m, "COUPLING_TYPE")
    .value("UNUSED_DOF", UNUSED_DOF)
    .value("LOCAL_DOF", LOCAL_DOF)
    .value("INTERFACE_DOF", INTERFACE_DOF)
    .value("NONWIREBASKET_DOF", NONWIREBASKET_DOF)
    .value("WIREBASKET_DOF", WIREBASKET_DOF)
    .value("EXTERNAL_DOF", EXTERNAL_DOF)
    .value("ANY_DOF", ANY_DOF)
    // .export_values()
    ;

  //////////////////////////////////////////////////////////////////////////////////////////

  py::class_<ElementRange, IntRange> (m, "ElementRange")
    .def(py::init<const MeshAccess&,VorB,IntRange>())
    .def("__iter__", [] (ElementRange &er)
      { return py::make_iterator(er.begin(), er.end()); },
      py::keep_alive<0,1>()
    );

  py::class_<FESpace::ElementRange,shared_ptr<FESpace::ElementRange>, IntRange> (m, "FESpaceElementRange")
    .def("__iter__", [] (FESpace::ElementRange &er)
      { return py::make_iterator(er.begin(), er.end()); },
      py::keep_alive<0,1>()
    );

  //////////////////////////////////////////////////////////////////////////////////////////

  py::class_<ElementId> (m, "ElementId", 
                         "an element identifier containing element number and Volume/Boundary flag")
    .def(py::init<VorB,int>())
    .def(py::init<int>())
    .def(py::init<Ngs_Element>())
    .def("__str__", &ToString<ElementId>)
    .def_property_readonly("nr", &ElementId::Nr, "the element number")    
    .def("VB", &ElementId::VB, "VorB of element")
    .def(py::self!=py::self)
    .def("__eq__" , [](ElementId &self, ElementId &other)
         { return !(self!=other); } )
    .def("__hash__" , &ElementId::Nr)
    ;
  
  m.def("BndElementId",[] (int nr) { return ElementId(BND,nr); },
          py::arg("nr"),
          "creates an element-id for a boundary element")
    ;


  //////////////////////////////////////////////////////////////////////////////////////////

  // TODO: make tuple not doing the right thing
  py::class_<Ngs_Element>(m, "Ngs_Element")
    .def_property_readonly("nr", &Ngs_Element::Nr, "the element number")    
    .def("VB", &Ngs_Element::VB, "VorB of element")   
    .def_property_readonly("vertices", [](Ngs_Element &el) {
        return py::cast(Array<int>(el.Vertices()));
        })//, "list of global vertex numbers")
    .def_property_readonly("edges", [](Ngs_Element &el) { return py::cast(Array<int>(el.Edges()));} ,
                  "list of global edge numbers")
    .def_property_readonly("faces", [](Ngs_Element &el) { return py::cast(Array<int>(el.Faces()));} ,
                  "list of global face numbers")
    .def_property_readonly("type", [](Ngs_Element &self)
        { return self.GetType(); },
        "geometric shape of element")
    .def_property_readonly("index", [](Ngs_Element &self)
        { return self.GetIndex(); },
        "material or boundary condition index")
    .def_property_readonly("mat", [](Ngs_Element & el)
                                         { return el.GetMaterial() ? *el.GetMaterial() : ""; },
                  "material or boundary condition label")
    ;

  py::implicitly_convertible <Ngs_Element, ElementId> ();
  // py::implicitly_convertible <ElementId, Ngs_Element> ();

  py::class_<FESpace::Element,Ngs_Element>(m, "FESpaceElement")
    .def_property_readonly("dofs",
                  [](FESpace::Element & el) 
                   {
                     py::list res;
                     Array<int> tmp (el.GetDofs());
                     for( int i : tmp)
                        res.append(py::cast(i));
                     return res;
                   },
                  "degrees of freedom of element"
                  )

    .def("GetLH",[](FESpace::Element & el) -> LocalHeap & 
                                  {
                                    return el.GetLH();
                                  },
         py::return_value_policy::reference
         )
    
    .def("GetFE",[](FESpace::Element & el)
                                  {
                                    return shared_ptr<FiniteElement>(const_cast<FiniteElement*>(&el.GetFE()), NOOP_Deleter);
                                  },
         py::return_value_policy::reference,
         "the finite element containing shape functions"
         )

    .def("GetTrafo",[](FESpace::Element & el) -> PyWrapper<ElementTransformation>
                                     {
                                       return shared_ptr<ElementTransformation>(const_cast<ElementTransformation*>(&el.GetTrafo()), NOOP_Deleter);
                                     },
         py::return_value_policy::reference,
         "the transformation from reference element to physical element"
         )

    ;
  //////////////////////////////////////////////////////////////////////////////////////////


  py::class_<GlobalDummyVariables> (m, "GlobalVariables")
    .def_property("msg_level", 
                 &GlobalDummyVariables::GetMsgLevel,
                 &GlobalDummyVariables::SetMsgLevel)
    .def_property("testout", 
                 &GlobalDummyVariables::GetTestoutFile,
                 &GlobalDummyVariables::SetTestoutFile)
    /*
    .def_property_readonly("pajetrace",
		  &GlobalDummyVariables::GetTestoutFile,
		 [] (GlobalDummyVariables&, bool use)
				  { TaskManager::SetPajeTrace(use); });
    */
    .def_property("pajetrace",
		  &GlobalDummyVariables::GetTestoutFile,
		 [] (GlobalDummyVariables&, int size)
				  {
                                    TaskManager::SetPajeTrace(size > 0);
                                    PajeTrace::SetMaxTracefileSize(size);
                                  })
    .def_property("numthreads",
		 [] (GlobalDummyVariables&)
				  {
                                    return TaskManager::GetMaxThreads ();
                                  },
		 [] (GlobalDummyVariables&, int numthreads)
				  {
                                    TaskManager::SetNumThreads (numthreads);
                                  })
    // &GlobalDummyVariables::SetTestoutFile)
    ;

  m.attr("ngsglobals") = py::cast(&globvar);

  //////////////////////////////////////////////////////////////////////////////////

//   PyExportArray<string>(m); //TODO

//   struct MeshAccess_pickle_suite : py::pickle_suite
//   {
//     static
//     py::tuple getinitargs(const MeshAccess & ma)
//     {
//       return py::make_tuple(); 
//     }
// 
//     static
//     py::tuple getstate(py::object o)
//     {
//       auto & ma = py::extract<MeshAccess const&>(o)();
//       stringstream str;
//       ma.SaveMesh(str);
//       return py::make_tuple (o.attr("__dict__"), str.str());
//     }
//     
//     static
//     void setstate(py::object o, py::tuple state)
//     {
//       auto & ma = py::extract<MeshAccess&>(o)();
// 
//       /*
//       if (len(state) != 2)
//         {
//           PyErr_SetObject(PyExc_ValueError,
//                           ("expected 2-item tuple in call to __setstate__; got %s"
//                            % state).ptr()
//                           );
//           throw_error_already_set();
//         }
//       */
// 
//       py::dict d = py::extract<py::dict>(o.attr("__dict__"))();
//       d.update(state[0]);
//       string s = py::extract<string>(state[1]);
//       stringstream str(s);
//       ma.LoadMesh (str);
//     }
// 
//     static bool getstate_manages_dict() { return true; }
//   };
// 
// 
//   struct FESpace_pickle_suite : py::pickle_suite
//   {
//     static
//     py::tuple getinitargs(py::object obj)
//     {
//       auto fes = py::extract<PyFES>(obj)().Get();
//       py::object m (fes->GetMeshAccess());
//       py::object flags = obj.attr("__dict__")["flags"];
//       flags["dim"] = fes->GetDimension();
//       return py::make_tuple(fes->type, m, flags, fes->GetOrder(), fes->IsComplex());
//     }
// 
//     static
//     py::tuple getstate(py::object o)
//     {
//       // auto & fes = py::extract<FESpace const&>(o)();
//       return py::make_tuple (o.attr("__dict__")); // , str.str());
//     }
//     
//     static
//     void setstate(py::object o, py::tuple state)
//     {
//       // auto & fes = py::extract<FESpace&>(o)();
//       py::dict d = py::extract<py::dict>(o.attr("__dict__"))();
//       d.update(state[0]);
//     }
// 
//     static bool getstate_manages_dict() { return true; }
//   };
  


  //////////////////////////////////////////////////////////////////////////////////////////

  py::class_<Region> (m, "Region", "a subset of volume or boundary elements")
    .def(py::init<shared_ptr<MeshAccess>,VorB,string>())
    .def("Mask",[](Region & reg)->BitArray { return reg.Mask(); })
    .def(py::self + py::self)
    .def(py::self + string())
    .def(py::self - py::self)
    .def(py::self - string())
    .def(~py::self)
    ;

  py::implicitly_convertible <Region, BitArray> ();


  //////////////////////////////////////////////////////////////////////////////////////////
  
  
  py::class_<MeshAccess, shared_ptr<MeshAccess>>(m, "Mesh", "the mesh", py::dynamic_attr())
    .def(py::init<shared_ptr<netgen::Mesh>>())
    .def("__ngsid__", [] ( MeshAccess & self)
        { return reinterpret_cast<std::uintptr_t>(&self); }  )
    
#ifndef PARALLEL
    .def("__init__",
         [](MeshAccess *instance, const string & filename)
                           { 
                             new (instance) MeshAccess(filename);
                           },
          py::arg("filename"))

#else

    .def("__init__",
         [](MeshAccess *instance, const string & filename,
                              py::object py_mpicomm)
                           { 
                             PyObject * py_mpicomm_ptr = py_mpicomm.ptr();
                             if (py_mpicomm_ptr != Py_None)
                               {
                                 MPI_Comm * comm = PyMPIComm_Get (py_mpicomm_ptr);
                                 ngs_comm = *comm;
                               }
                             else
                               ngs_comm = MPI_COMM_WORLD;

                             NGSOStream::SetGlobalActive (MyMPI_GetId()==0);
                             new (instance) MeshAccess (filename, ngs_comm);
                           },
          py::arg("filename"), py::arg("mpicomm")=DummyArgument())
#endif

    
    .def("__eq__",
         [] (shared_ptr<MeshAccess> self, shared_ptr<MeshAccess> other)
           {
             return self == other;
           })

    .def("__getstate__", [] (py::object self_object) {
        auto self = self_object.cast<MeshAccess>();
        stringstream str;
        self.SaveMesh(str);
        auto dict = self_object.attr("__dict__");
        return py::make_tuple(py::cast(str.str()), dict);
       })
    .def("__setstate__", [](MeshAccess *instance, py::tuple state) {
        string s = state[0].cast<string>();
        stringstream str(s);
        new (instance) MeshAccess();
        instance->LoadMesh (str);
         // restore dynamic attributes (necessary for persistent id in NgsPickler)
         py::object self_object = py::cast(*instance);
         self_object.attr("__dict__") = state[1];
    })
    .def("LoadMesh", static_cast<void(MeshAccess::*)(const string &)>(&MeshAccess::LoadMesh),
         "Load mesh from file")
    
    .def("Elements", static_cast<ElementRange(MeshAccess::*)(VorB)const> (&MeshAccess::Elements),
         (py::arg("VOL_or_BND")=VOL))

    .def("__getitem__", static_cast<Ngs_Element(MeshAccess::*)(ElementId)const> (&MeshAccess::operator[]))

    .def ("GetNE", static_cast<size_t(MeshAccess::*)(VorB)const> (&MeshAccess::GetNE))
    .def_property_readonly ("nv", &MeshAccess::GetNV, "number of vertices")
    .def_property_readonly ("ne",  static_cast<size_t(MeshAccess::*)()const> (&MeshAccess::GetNE), "number of volume elements")
    .def_property_readonly ("dim", &MeshAccess::GetDimension, "mesh dimension")
    .def_property_readonly ("ngmesh", &MeshAccess::GetNetgenMesh, "netgen mesh")
    .def ("GetTrafo", 
          static_cast<ElementTransformation&(MeshAccess::*)(ElementId,Allocator&)const>
          (&MeshAccess::GetTrafo), 
          py::return_value_policy::reference)

    .def ("GetTrafo", FunctionPointer([](MeshAccess & ma, ElementId id)
                                      {
                                        return &ma.GetTrafo(id, global_alloc);
                                      }),
          py::return_value_policy::take_ownership)

    // .def("SetDeformation", &MeshAccess::SetDeformation)
    .def("SetDeformation", FunctionPointer
	 ([](MeshAccess & ma, PyGF gf)
          { ma.SetDeformation(gf.Get()); }))
    //old
    //.def("SetRadialPML", &MeshAccess::SetRadialPML)
    .def("SetPML", FunctionPointer
	 ([](MeshAccess & ma,  PyPML apml, py::object definedon)
          {
            //shared_ptr<PML_Transformation> spml = apml.CreateDim(ma.GetDimension());
            if (py::extract<int>(definedon).check())
              {
                ma.SetPML(apml.Get(), py::extract<int>(definedon)()-1);
              }

            if (py::isinstance<py::str>(definedon))
              {
                std::regex pattern(definedon.cast<string>());
                for (int i = 0; i < ma.GetNDomains(); i++)
                  if (std::regex_match (ma.GetDomainMaterial(i), pattern))
                    ma.SetPML(apml.Get(), i);
              }
          }),
   py::arg("pmltrafo"),py::arg("definedon"),
   "set PML transformation on domain"
   )
    .def("UnSetPML", &MeshAccess::UnSetPML)
    .def("GetPMLTrafos", [](MeshAccess & ma) {
      py::list pml_trafos(ma.GetNDomains());
	    for (int i : Range(ma.GetNDomains()))
        {
        if (ma.GetPMLTrafos()[i])
  	      pml_trafos[i] = py::cast(PyPML(ma.GetPMLTrafos()[i]->CreateDim(0)));
        else
          pml_trafos[i] = py::none();
        }
	    return pml_trafos;
        },
        "returns list of pml transformations"
        )
    .def("GetPMLTrafo", [](MeshAccess & ma, int domnr) {
	      if (ma.GetPMLTrafos()[domnr])
     	    return py::cast(PyPML(ma.GetPMLTrafos()[domnr]->CreateDim(0)));
        else
          throw Exception("No PML Trafo set"); 
        },
        py::arg("dom")=0,
        "returns pml transformation on domain dom"
        )
    .def("UnsetDeformation", FunctionPointer
	 ([](MeshAccess & ma){ ma.SetDeformation(nullptr);}))
    
    .def("GetMaterials",
	 [](const MeshAccess & ma)
	  {
            py::list materials(ma.GetNDomains());
	    for (int i : Range(ma.GetNDomains()))
	      materials[i] = py::cast(ma.GetDomainMaterial(i));
	    return materials;
	  },
         "returns list of materials"
         )

    .def("Materials",
	 [](shared_ptr<MeshAccess> ma, string pattern) 
	  {
            return new Region (ma, VOL, pattern);
	  },
         py::arg("pattern"),
         "returns mesh-region matching the given regex pattern",
         py::return_value_policy::take_ownership
         )
    
    .def("GetBoundaries",
	 [](const MeshAccess & ma)
	  {
            py::list materials(ma.GetNBoundaries());
	    for (int i : Range(ma.GetNBoundaries()))
	      materials[i] = py::cast(ma.GetBCNumBCName(i));
	    return materials;
	  },
         "returns list of boundary conditions"
         )

    .def("Boundaries",
	 [](shared_ptr<MeshAccess> ma, string pattern)
	  {
            return new Region (ma, BND, pattern);
	  },
         py::arg("pattern"),
         "returns boundary mesh-region matching the given regex pattern",
         py::return_value_policy::take_ownership
         )
    .def("GetBBoundaries",
	 [](const MeshAccess & ma)
	  {
	    py::list bboundaries(ma.GetNBBoundaries());
	    for (int i : Range(ma.GetNBBoundaries()))
	      bboundaries[i] = py::cast(ma.GetCD2NumCD2Name(i));
	    return bboundaries;
	  },
	 "returns list of boundary conditions for co dimension 2"
	 )
    .def("BBoundaries", FunctionPointer
	 ([](shared_ptr<MeshAccess> ma, string pattern)
	  {
	    return new Region (ma, BBND, pattern);
	  }),
	 (py::arg("self"), py::arg("pattern")),
	 "returns co dim 2 boundary mesh-region matching the given regex pattern",
	 py::return_value_policy::take_ownership
	 )

    .def("Refine",
         [](MeshAccess & ma)
          {
            ma.Refine();
          },
         "local mesh refinement based on marked elements, uses element-bisection algorithm")

    .def("RefineHP",
         [](MeshAccess & ma, int levels, double factor)
          {
            Ng_HPRefinement(levels, factor);
            ma.UpdateBuffers();
          },
         py::arg("levels"), py::arg("factor")=0.125,
         "geometric mesh refinement towards marked vertices and edges, uses factor for placement of new points"
         )

    .def("SetRefinementFlag", &MeshAccess::SetRefinementFlag,
         "set refinementflag for mesh-refinement")

    .def("GetParentElement", &MeshAccess::GetParentElement)
    .def("GetParentVertices", FunctionPointer
         ([](MeshAccess & ma, int vnum)
          {
            Array<int> parents(2);
            ma.GetParentNodes (vnum, &parents[0]);
            return py::make_tuple(parents[0], parents[1]);
          }))
    
    .def("Curve",
         [](MeshAccess & ma, int order)
          {
            Ng_HighOrder(order);
          },
         py::arg("order"))



    .def("__call__",
         [](MeshAccess & ma, double x, double y, double z, VorB vb) 
          {
            IntegrationPoint ip;
            int elnr;
            if (vb == VOL)
              elnr = ma.FindElementOfPoint(Vec<3>(x, y, z), ip, true);
            else
              elnr = ma.FindSurfaceElementOfPoint(Vec<3>(x, y, z), ip, true);
              
            if (elnr < 0) throw Exception ("point out of domain");

            ElementTransformation & trafo = ma.GetTrafo(ElementId(vb, elnr), global_alloc);
            BaseMappedIntegrationPoint & mip = trafo(ip, global_alloc);
            mip.SetOwnsTrafo(true);
            return &mip;
          } 
          , 
         py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0,
         py::arg("VOL_or_BND") = VOL,
         py::return_value_policy::reference
         )

    .def("Contains",
         [](MeshAccess & ma, double x, double y, double z) 
          {
            IntegrationPoint ip;
            int elnr = ma.FindElementOfPoint(Vec<3>(x, y, z), ip, true);
            return (elnr >= 0);
          }, 
         py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0
         )

    ;

  //////////////////////////////////////////////////////////////////////////////////////////
  
  py::class_<NGS_Object, shared_ptr<NGS_Object>>(m, "NGS_Object")
    .def_property_readonly("name", FunctionPointer
                  ([](const NGS_Object & self)->string { return self.GetName();}))
    ;

  //////////////////////////////////////////////////////////////////////////////////////////

  typedef PyWrapper<CoefficientFunction> PyCF;

  py::class_<PyProxyFunction, PyCF> (m, "ProxyFunction")
    .def("Deriv", FunctionPointer
         ([](const PyProxyFunction self)
          { return PyProxyFunction(self->Deriv()); }),
         "take canonical derivative (grad, curl, div)")
    .def("Trace", FunctionPointer
         ([](const PyProxyFunction self)
          { return PyProxyFunction(self->Trace()); }),
         "take canonical boundary trace")
    .def("Other", FunctionPointer
         ([](const PyProxyFunction self, py::object bnd) 
          {
            if (py::extract<double> (bnd).check())
              return PyProxyFunction(self->Other(make_shared<ConstantCoefficientFunction>(py::extract<double> (bnd)())));
            if (py::extract<PyCF> (bnd).check())
              return PyProxyFunction(self->Other(py::extract<PyCF> (bnd)().Get()));
            else
              return PyProxyFunction(self->Other(nullptr));
          }),
         "take value from neighbour element (DG)",
          py::arg("bnd") = DummyArgument()
         )
    .def_property_readonly("derivname",
                  [](const PyProxyFunction self) -> string
                   {
                     if (!self->Deriv()) return "";
                     return self->DerivEvaluator()->Name();
                   })
    .def("Operator",
         [] (const PyProxyFunction self, string name) -> py::object 
          {
            auto op = self->GetAdditionalProxy(name);
            if (op)
              return py::cast(PyProxyFunction(op));
            return py::none();
          })
    ;




  struct OrderProxy 
  {
    FESpace & fes;
    OrderProxy (FESpace & afes) : fes(afes) { ; }
  };


  py::class_<OrderProxy> (m, "OrderProxy")
    .def("__setitem__",
         [] (OrderProxy & self, ElementId ei, int o) 
          {
            cout << "set order of el " << ei << " to order " << o << endl;
            cout << "(not implemented)" << endl;
          })

    .def("__setitem__",
         [] (OrderProxy & self, ELEMENT_TYPE et, int o) 
          {
            cout << "set order of eltype " << et << " to order " << o << endl;
            self.fes.SetBonusOrder (et, o - self.fes.GetOrder());

            LocalHeap lh (100000, "FESpace::Update-heap", true);
            self.fes.Update(lh);
            self.fes.FinalizeUpdate(lh);
          })
    
    .def("__setitem__",
         [] (OrderProxy & self, NODE_TYPE nt, int o) 
          {
            cout << "set order of nodetype " << int(nt) << " to order " << o << endl;
            nt = StdNodeType (nt, self.fes.GetMeshAccess()->GetDimension());
            cout << "canonical nt = " << int(nt) << endl;
            int bonus = o-self.fes.GetOrder();
            switch (nt)
              {
              case 1: 
                self.fes.SetBonusOrder(ET_SEGM, bonus); break;
              case 2: 
                self.fes.SetBonusOrder(ET_QUAD, bonus); 
                self.fes.SetBonusOrder(ET_TRIG, bonus); break;
              case 3: 
                self.fes.SetBonusOrder(ET_TET, bonus); 
                self.fes.SetBonusOrder(ET_PRISM, bonus);
                self.fes.SetBonusOrder(ET_PYRAMID, bonus);
                self.fes.SetBonusOrder(ET_HEX, bonus); break;
              default: ;
              }

            LocalHeap lh (100000, "FESpace::Update-heap", true);
            self.fes.Update(lh);
            self.fes.FinalizeUpdate(lh);
          })

    .def("__setitem__",
         [] (OrderProxy & self, NODE_TYPE nt, int nr, int o) 
          {
            cout << "set order of " << nt << " " << nr << " to " << o << endl;
            cout << "(not implemented)" << endl;
          })

    .def("__setitem__",
         [] (OrderProxy & self, py::tuple tup, int o) 
          {
            NODE_TYPE nt = py::extract<NODE_TYPE>(tup[0])();
            int nr = py::extract<int>(tup[1])();
            cout << "set order of " << nt << " " << nr << " to " << o << endl;
            cout << "(not implemented)" << endl;
          })
    

    

    /*
    .def("__setitem__", FunctionPointer([] (OrderProxy & self, py::slice inds, int o) 
                                        {
                                          cout << "set order to slice, o = " <<o << endl;
                                          auto ndof = self.fes.GetNDof();
                                          py::object indices = inds.attr("indices")(ndof);
                                          int start = py::extract<int> (indices[0]);
                                          int stop = py::extract<int> (indices[1]);
                                          int step = py::extract<int> (indices[2]);
                                          cout << "start = " << start << ", stop = " << stop << ", step = " << step << endl;
                                        }))

    .def("__setitem__", FunctionPointer([] (OrderProxy & self, py::list inds, int o) 
                                        {
                                          cout << "set order list" << endl;

                                          for (int i = 0; i < len(inds); i++)
                                            cout << py::extract<int> (inds[i]) << endl;
                                        }))
    */

    /*
    .def("__setitem__", FunctionPointer([] (OrderProxy & self, py::object generator, int o) 
                                        {
                                          cout << "general setitem called" << endl;

                                          if (py::extract<int> (generator).check())
                                            {
                                              cout << " set order, int" << endl;
                                              return;
                                            }

                                          if (py::extract<ElementId> (generator).check())
                                            {
                                              cout << " set order, elid" << endl;
                                              return;
                                            }
                                          if (py::extract<py::slice> (generator).check())
                                            {
                                              cout << " set order, slice" << endl;
                                              return;
                                            }
                                          
                                          cout << "set order from generator" << endl;
                                          try
                                            {
                                              auto iter = generator.attr("__iter__")();
                                              while (1)
                                                {
                                                  auto el = iter.attr("__next__")();
                                                  cout << py::extract<int> (el) << " ";
                                                }
                                            }
                                          catch (py::error_already_set&) 
                                            { 
                                              if (PyErr_ExceptionMatches (PyExc_StopIteration))
                                                {
                                                  cout << endl;
                                                  PyErr_Clear();
                                                }
                                              else
                                                {
                                                  cout << "some other error" << endl;
                                                }
                                            };
                                        }))
    */
    ;


  static size_t global_heapsize = 1000000;
  static LocalHeap glh(global_heapsize, "python-comp lh", true);
  m.def("SetHeapSize", [](size_t heapsize)
                                         {
                                           if (heapsize > global_heapsize)
                                             {
                                               global_heapsize = heapsize;
                                               glh = LocalHeap (heapsize, "python-comp lh", true);
                                             }
                                         });

  m.def("SetTestoutFile", [](string filename)
                                            {
                                              testout = new ofstream (filename);
                                            });
  
  //////////////////////////////////////////////////////////////////////////////////////////


  auto fes_dummy_init = [](PyFES *instance, shared_ptr<MeshAccess> ma, const string & type, 
                              py::dict bpflags, int order, bool is_complex,
                              py::object dirichlet, py::object definedon, int dim)
                           {
                             Flags flags = py::extract<Flags> (bpflags)();

                             if (order > -1) {
			       flags.SetFlag ("order", order);
// 			       bpflags["order"] = py::cast(order);
                             }
                             if (dim > -1) {
			       flags.SetFlag ("dim", dim);
// 			       bpflags["dim"] = py::cast(dim);
                             }
                             if (is_complex) {
			       flags.SetFlag ("complex");
// 			       bpflags["complex"] = py::cast(is_complex);
			     }

                             if (py::isinstance<py::list>(dirichlet)) {
                               flags.SetFlag("dirichlet", makeCArray<double>(py::list(dirichlet)));
// 			       bpflags["dirichlet"] = dirlist();
			     }

                             if (py::isinstance<py::str>(dirichlet))
                               {
                                 std::regex pattern(dirichlet.cast<string>());
                                 Array<double> dirlist;
                                 for (int i = 0; i < ma->GetNBoundaries(); i++)
                                   if (std::regex_match (ma->GetBCNumBCName(i), pattern))
                                     dirlist.Append (i+1);
                                 flags.SetFlag("dirichlet", dirlist);
// 				 bpflags["dirichlet"] = py::cast(dirlist);
                               }

                             if (py::isinstance<py::str>(definedon))
                               {
                                 std::regex pattern(definedon.cast<string>());
                                 Array<double> defonlist;
                                 for (int i = 0; i < ma->GetNDomains(); i++)
                                   if (regex_match(ma->GetDomainMaterial(i), pattern))
                                     defonlist.Append(i+1);
                                 flags.SetFlag ("definedon", defonlist);
// 				 bpflags["definedon"] = py::cast(defonlist);
                               }
                             py::extract<py::list> definedon_list(definedon);
                             if (definedon_list.check())
                               flags.SetFlag ("definedon", makeCArray<double> (definedon));
                             py::extract<Region> definedon_reg(definedon);
                             if (definedon_reg.check() && definedon_reg().IsVolume())
                               {
                                 Array<double> defonlist;
                                 for (int i = 0; i < definedon_reg().Mask().Size(); i++)
                                   if (definedon_reg().Mask().Test(i))
                                     defonlist.Append(i+1);
                                 flags.SetFlag ("definedon", defonlist);
// 				 bpflags["definedon"] = py::cast(defonlist);
                               }
                             
                             
                             auto fes = CreateFESpace (type, ma, flags); 
                             LocalHeap lh (1000000, "FESpace::Update-heap");
                             fes->Update(lh);
                             fes->FinalizeUpdate(lh);
                             new (instance) PyFES(fes);
                             };

  py::class_<PyFES>(m, "FESpace",  "a finite element space", py::dynamic_attr())
    // the raw - constructor
    .def("__init__", 
         [&](PyFES *instance, const string & type, py::object bp_ma, 
                             py::dict bp_flags, int order, bool is_complex,
                             py::object dirichlet, py::object definedon, int dim)
                          {
                            shared_ptr<MeshAccess> ma = py::extract<shared_ptr<MeshAccess>>(bp_ma)();
                            fes_dummy_init(instance, ma, type, bp_flags, order, is_complex, dirichlet, definedon, dim);
//                              py::cast(*instance).attr("flags") = py::cast(bp_flags);
			     
                           },
         py::arg("type"), py::arg("mesh"), py::arg("flags") = py::dict(), 
           py::arg("order")=-1, 
           py::arg("complex")=false, 
           py::arg("dirichlet")=DummyArgument(),
           py::arg("definedon")=DummyArgument(),
          py::arg("dim")=-1,
         "allowed types are: 'h1ho', 'l2ho', 'hcurlho', 'hdivho' etc."
         )
    

    
    .def("__init__",
         [](PyFES *instance, py::list lspaces, py::dict bpflags)
                           {
                             Flags flags = py::extract<Flags> (bpflags)();

                             Array<shared_ptr<FESpace>> spaces;
                             for( auto fes : lspaces )
                               spaces.Append(py::extract<PyFES>(fes)().Get());
                             if (spaces.Size() == 0)
                               throw Exception("Compound space must have at least one space");
                             int dim = spaces[0]->GetDimension();
                             for (auto space : spaces)
                               if (space->GetDimension() != dim)
                                 throw Exception("Compound space of spaces with different dimensions is not allowed");
                             flags.SetFlag ("dim", dim);
                             
                             bool is_complex = spaces[0]->IsComplex() || flags.GetDefineFlag("complex");
                             for (auto space : spaces)
                               if (space->IsComplex() != is_complex)
                                 throw Exception("Compound space of spaces with complex and real spaces is not allowed");
                             if (is_complex)
                               flags.SetFlag ("complex");
                             
                             shared_ptr<FESpace> fes = make_shared<CompoundFESpace> (spaces[0]->GetMeshAccess(), spaces, flags);
                             LocalHeap lh (1000000, "FESpace::Update-heap");
                             fes->Update(lh);
                             fes->FinalizeUpdate(lh);
                             new (instance) PyFES(fes);
//                              py::cast(*instance).attr("flags") = bpflags;
                           },
          py::arg("spaces"), py::arg("flags") = py::dict(),
         "construct compound-FESpace from list of component spaces"
         )
    .def("__ngsid__", [] ( PyFES & self)
        { return reinterpret_cast<std::uintptr_t>(self.Get().get()); } )
    .def("__getstate__", [] (py::object self_object) {
        auto self = self_object.cast<PyFES>();
        auto dict = self_object.attr("__dict__");
        auto mesh = self->GetMeshAccess();
        return py::make_tuple( self->type, mesh, dict );
     })
    .def("__setstate__", [] (PyFES &self, py::tuple t) {
        auto flags = t[2]["flags"].cast<Flags>();
        auto fes = CreateFESpace (t[0].cast<string>(), t[1].cast<shared_ptr<MeshAccess>>(), flags);
        LocalHeap lh (1000000, "FESpace::Update-heap");
        fes->Update(lh);
        fes->FinalizeUpdate(lh);
        new (&self) PyFES(fes);
        py::cast(self).attr("__dict__") = t[2];
     })
    .def("Update", [](PyFES & self, int heapsize)
                                   { 
                                     LocalHeap lh (heapsize, "FESpace::Update-heap");
                                     self->Update(lh);
                                     self->FinalizeUpdate(lh);
                                   },
         py::arg("heapsize")=1000000,
         "Update space, use after mesh-refinement")

    .def_property_readonly ("ndof", [](PyFES & self) { return self->GetNDof(); }, 
                   "number of degrees of freedom")

    .def_property_readonly ("ndofglobal", [](PyFES & self) { return self->GetNDofGlobal(); }, 
                   "global number of dofs on MPI-distributed mesh")
    // .def("__str__", &ToString<FESpace>)
    .def("__str__", [] (PyFES & self) { return ToString(*self.Get()); } )

    // .def_property_readonly("mesh", FunctionPointer ([](FESpace & self) -> shared_ptr<MeshAccess>
    // { return self.GetMeshAccess(); }))

    .def_property_readonly("order", FunctionPointer([] (PyFES & self) { return OrderProxy(*self.Get()); }),
                  "proxy to set order for individual nodes")
    .def_property_readonly("globalorder", FunctionPointer([] (PyFES & self) { return self->GetOrder(); }),
                  "query global order of space")    
    .def_property_readonly("type", FunctionPointer([] (PyFES & self) { return self->type; }),
                  "type of finite element space")    

    .def("Elements", 
         FunctionPointer([](PyFES & self, VorB vb, int heapsize) 
                         {
                           return make_shared<FESpace::ElementRange> (self->Elements(vb, heapsize));
                         }),
         py::arg("VOL_or_BND")=VOL,py::arg("heapsize")=10000, "Returns Iterator over elements of the finite element space")

    .def("Elements", 
         FunctionPointer([](PyFES & self, VorB vb, LocalHeap & lh) 
                         {
                           return make_shared<FESpace::ElementRange> (self->Elements(vb, lh));
                         }),
         py::arg("VOL_or_BND")=VOL, py::arg("heap"), "Returns Iterator over elements of the finite element space, uses given LocalHeap")

    /*
    .def("Elements", 
         FunctionPointer([](FESpace & self, VorB vb, LocalHeap & lh, int heapsize) 
                         {
                           cout << "lh.avail = " << lh.Available() << endl;
                           return make_shared<FESpace::ElementRange> (self.Elements(vb, heapsize));
                         }),
         py::arg("VOL_or_BND")=VOL, 
          py::arg("heap")=LocalHeap(0), py::arg("heapsize")=10000)
    */

    .def("GetDofNrs", FunctionPointer([](PyFES & self, ElementId ei) 
                                   {
                                     Array<int> tmp; self->GetDofNrs(ei,tmp); 
                                     py::tuple tuple(tmp.Size());
                                     for( auto i : Range(tmp))
                                        tuple[i] = py::int_(tmp[i]);
                                     return tuple;
                                   }), "Get the degrees of freedom for the Element Given by ElementId")

    .def("CouplingType", FunctionPointer ([](PyFES & self, int dofnr) -> COUPLING_TYPE
                                          { return self.Get()->GetDofCouplingType(dofnr); }),
         py::arg("dofnr"),
         "get coupling type of a degree of freedom"
         )
    .def("SetCouplingType", [](PyFES & self, int dofnr, COUPLING_TYPE ct) 
                                             { return self.Get()->SetDofCouplingType(dofnr,ct); },
         py::arg("dofnr"), py::arg("coupling_type"),
         "set coupling type of a degree of freedom"
         )

    /*
    .def ("GetFE", 
          static_cast<FiniteElement&(FESpace::*)(ElementId,Allocator&)const>
          (&FESpace::GetFE), 
          py::return_value_policy::reference)
    */
    .def ("GetFE", FunctionPointer([](PyFES & self, ElementId ei) -> py::object
                                   {
                                     Allocator alloc;

                                     auto fe = shared_ptr<FiniteElement> (&self->GetFE(ei, alloc), NOOP_Deleter);

                                     auto scalfe = dynamic_pointer_cast<BaseScalarFiniteElement> (fe);
                                     if (scalfe) return py::cast(scalfe);

                                     return py::cast(fe);

                                   }), "Creates the FiniteElement for an ElementId")
    
    .def ("GetFE", FunctionPointer([](PyFES & self, ElementId ei, LocalHeap & lh)
                                   {
                                     return shared_ptr<FiniteElement>(&self->GetFE(ei, lh), NOOP_Deleter);
                                   }),
          py::return_value_policy::reference, "Creates the FiniteElement for an ElementId on the given LocalHeap")


    .def("FreeDofs", FunctionPointer
         ( [] (const PyFES &self, bool coupling) { return self->GetFreeDofs(coupling); } ),
         py::arg("coupling")=false, "Gives the degrees of freedom of the finite element space")

    .def("Range", FunctionPointer
         ( [] (const PyFES & self, int comp) -> py::slice
           {
             auto compspace = dynamic_pointer_cast<CompoundFESpace> (self.Get());
             if (!compspace)
               throw py::type_error("'Range' is available only for product spaces");
             IntRange r = compspace->GetRange(comp);
             return py::slice(py::int_(r.First()), py::int_(r.Next()),1);
           }))

    .def_property_readonly("components", FunctionPointer
                  ([](PyFES & self)-> py::tuple
                   { 
                     auto compspace = dynamic_pointer_cast<CompoundFESpace> (self.Get());
                     if (!compspace)
                       throw py::type_error("'components' is available only for product spaces");
                     py::tuple vecs(compspace->GetNSpaces());
                     for (int i = 0; i < compspace -> GetNSpaces(); i++) 
                       vecs[i]= py::cast( PyFES((*compspace)[i]) );
                     return vecs;
                   }),
                  "list of gridfunctions for compound gridfunction")

    .def("TrialFunction",
         [] (const PyFES & self) 
           {
             return MakeProxyFunction (*self.Get(), false);
           }, docu_string("Gives a proxy to be used as a trialfunction in :any:`Symbolic Integrators`"))
    .def("TestFunction",
         [] (const PyFES & self) 
           {
             return MakeProxyFunction (*self.Get(), true);
           }, docu_string("Gives a proxy to be used as a testfunction for :any:`Symbolic Integrators`"))

    .def("SolveM", FunctionPointer
        ( [] (const PyFES & self,
              PyCF rho, PyBaseVector vec, int heapsize)
          {
            // LocalHeap lh(heapsize, "solveM - lh", true);
            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
            self->SolveM(*rho.Get(), *vec, glh);
          }),
         py::arg("rho"), py::arg("vec"), py::arg("heapsize")=1000000)
        
    .def("__eq__", FunctionPointer
         ( [] (PyFES self, PyFES other)
           {
             return self.Get() == other.Get();
           }))
    ;
  typedef PyWrapperDerived<HCurlHighOrderFESpace, FESpace> PyHCurl;
  typedef PyWrapperDerived<CompoundFESpace, FESpace> PyCompoundFES;

  py::class_<PyHCurl, PyFES>
    (m, "HCurlFunctionsWrap")
    .def("CreateGradient", FunctionPointer([](PyFES & self) {
	  auto hcurl = dynamic_pointer_cast<HCurlHighOrderFESpace>(self.Get());
	  auto fesh1 = hcurl->CreateGradientSpace();
	  shared_ptr<BaseMatrix> grad = hcurl->CreateGradient(*fesh1);
	  auto fes = new PyFES(fesh1);
	  return py::make_tuple(grad, fes);
	}))
    ;
  
  py::class_<PyCompoundFES, PyFES>
    (m, "CompoundFESpace")
    .def("Range", &CompoundFESpace::GetRange)
    ;

  //////////////////////////////////////////////////////////////////////////////////////////
  

//   struct GF_pickle_suite : py::pickle_suite
//   {
//     static
//     py::tuple getinitargs(py::object obj)
//     {
//       auto gf = py::extract<PyGF>(obj)().Get();
//       py::object space = obj.attr("__dict__")["space"];
//       return py::make_tuple(space, gf->GetName());
//     }
// 
//     static
//     py::tuple getstate(py::object obj)
//     {
//       auto gf = py::extract<PyGF>(obj)().Get();
//       py::object bp_vec(gf->GetVectorPtr());
//       return py::make_tuple (obj.attr("__dict__"), bp_vec);
//     }
//     
//     static
//     void setstate(py::object obj, py::tuple state)
//     {
//       auto gf = py::extract<PyGF>(obj)().Get();
//       py::dict d = py::extract<py::dict>(obj.attr("__dict__"))();
//       d.update(state[0]);
//       gf->GetVector() = *py::extract<PyBaseVector> (state[1])();
//     }
// 
//     static bool getstate_manages_dict() { return true; }
//   };
  


  
  py::class_<PyGF, PyCF>
    (m, "GridFunction",  "a field approximated in some finite element space", py::dynamic_attr())


    
    // raw - constructor
    .def("__init__",
         [](PyGF *instance, py::object bp_fespace, string name, py::object multidim)
                          {
                            auto fespace = py::extract<PyFES>(bp_fespace)();

                            Flags flags;
                            flags.SetFlag ("novisual");
                            if (py::extract<int>(multidim).check())
                              flags.SetFlag ("multidim", py::extract<int>(multidim)());
                            auto gf = CreateGridFunction (fespace.Get(), name, flags);
                            gf->Update();
                            new (instance) PyGF(gf);
			    py::cast(*instance).attr("__dict__")["space"] = bp_fespace;
                          },
         py::arg("space"), py::arg("name")="gfu", py::arg("multidim")=DummyArgument(),
         "creates a gridfunction in finite element space"
         )
    .def("__ngsid__", FunctionPointer( [] ( PyGF self)
        { return reinterpret_cast<std::uintptr_t>(self.Get().get()); } ) )
    .def("__getstate__", [] (py::object self_object) {
        auto self = self_object.cast<PyGF>();
        auto vec = PyBaseVector(self->GetVectorPtr());
	auto fes = self_object.attr("space");
	auto perid  = self_object.attr("__persistent_id__");
        return py::make_tuple(fes, self->GetName(), vec, self->GetMultiDim(),perid);
        })
    .def("__setstate__", [] (PyGF &self, py::tuple t) {
         auto fespace = t[0].cast<PyFES>();
         Flags flags;
         flags.SetFlag ("multidim", py::extract<int>(t[3])());
         auto gf = CreateGridFunction (fespace.Get(), t[1].cast<string>(), flags);
         gf->Update();
	 gf->GetVector() = *t[2].cast<PyBaseVector>();
         new (&self) PyGF(gf);
         py::object self_object = py::cast(self);
	 self_object.attr("__persistent_id__") = t[4];
         })
    // .def("__str__", &ToString<GF>)
    .def("__str__", [] (PyGF & self) { return ToString(*self.Get()); } )
    .def_property_readonly("space", FunctionPointer([](py::object self) -> py::object
                                           {
                                             py::dict d = py::extract<py::dict>(self.attr("__dict__"))();
                                             // if gridfunction is created from python, it has the space attribute
                                             if (d.contains("space"))
                                               return d["space"];

                                             // if not, make a new python space object from C++ space
                                             return py::cast(PyFES(py::extract<PyGF>(self)()->GetFESpace()));
                                           }),
                  "the finite element space")
    // .def_property_readonly ("space", &GF::GetFESpace, "the finite element spaces")
    .def("Update", FunctionPointer ([](PyGF self) { self->Update(); }),
         "update vector size to finite element space dimension after mesh refinement")
    
    .def("Save", FunctionPointer([](PyGF self, string filename)
                                 {
                                   ofstream out(filename, ios::binary);
                                   self->Save(out);
                                 }))
    .def("Load", FunctionPointer([](PyGF self, string filename)
                                 {
                                   ifstream in(filename, ios::binary);
                                   self->Load(in);
                                 }))
    
    .def("Set", FunctionPointer
         ([](PyGF self, PyCF cf,
             VorB boundary, py::object definedon, int heapsize, py::object heap)
          {
             shared_ptr<TPHighOrderFESpace> tpspace = dynamic_pointer_cast<TPHighOrderFESpace>(self.Get()->GetFESpace());
             if(tpspace)
             {
               Transfer2TPMesh(cf.Get().get(),self.Get().get());
               return;
            }          
            Region * reg = nullptr;
            if (py::extract<Region&> (definedon).check())
              reg = &py::extract<Region&>(definedon)();
            
            if (py::extract<LocalHeap&> (heap).check())
              {
                LocalHeap & lh = py::extract<LocalHeap&> (heap)();
                if (reg)
                  SetValues (cf.Get(), *self.Get(), *reg, NULL, lh);
                else
                  SetValues (cf.Get(), *self.Get(), boundary, NULL, lh);
                return;
              }

            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
            // LocalHeap lh(heapsize, "GridFunction::Set-lh", true);
            if (reg)
              SetValues (cf.Get(), *self.Get(), *reg, NULL, glh);
            else
              SetValues (cf.Get(), *self.Get(), boundary, NULL, glh);
          }),
          py::arg("coefficient"),
          py::arg("VOL_or_BND")=VOL,
          py::arg("definedon")=DummyArgument(),
          py::arg("heapsize")=1000000, py::arg("heap")=DummyArgument(),
         "Set values"
      )


    .def_property_readonly("components", FunctionPointer
                  ([](PyGF self)-> py::tuple
                   { 
                     py::tuple vecs(self->GetNComponents());
                     for (int i = 0; i < self->GetNComponents(); i++)
                       vecs[i] = py::cast(PyGF(self->GetComponent(i)));
                     return vecs;
                   }),
                  "list of gridfunctions for compound gridfunction")

    .def_property_readonly("vec",
                  FunctionPointer([](PyGF self) -> PyBaseVector { return self->GetVectorPtr(); }),
                  "coefficient vector")

    .def_property_readonly("vecs", FunctionPointer
                  ([](PyGF self)-> py::list
                   { 
                     py::list vecs(self->GetMultiDim());
                     for (int i = 0; i < self->GetMultiDim(); i++)
                       vecs[i] = py::cast(PyBaseVector(self->GetVectorPtr(i)));
                     return vecs;
                   }),
                  "list of coefficient vectors for multi-dim gridfunction")

    /*
    .def("CF", FunctionPointer
         ([](shared_ptr<GF> self) -> shared_ptr<CoefficientFunction>
          {
            return make_shared<GridFunctionCoefficientFunction> (self);
          }))

    .def("CF", FunctionPointer
         ([](shared_ptr<GF> self, shared_ptr<DifferentialOperator> diffop)
          -> shared_ptr<CoefficientFunction>
          {
            return make_shared<GridFunctionCoefficientFunction> (self, diffop);
          }))
    */
    .def("Deriv", FunctionPointer
         ([](PyGF self) -> PyCF
          {
            auto sp = make_shared<GridFunctionCoefficientFunction> (self.Get(),
                                                                    self->GetFESpace()->GetFluxEvaluator(),
                                                                    self->GetFESpace()->GetFluxEvaluator(BND));
            // sp->SetComplex(self->GetFESpace()->IsComplex()); 
            sp->SetDimensions(sp->Dimensions());
            return PyCF(sp);
          }))

    .def("Operator", FunctionPointer
         ([](PyGF self, string name) -> py::object // shared_ptr<CoefficientFunction>
          {
            if (self->GetFESpace()->GetAdditionalEvaluators().Used(name))
              {
                auto diffop = self->GetFESpace()->GetAdditionalEvaluators()[name];
                cout << "diffop is " << typeid(*diffop).name() << endl;
                auto coef = make_shared<GridFunctionCoefficientFunction> (self.Get(), diffop);
                coef->SetDimension(diffop->Dim());
                return py::cast(PyCF(coef));
              }
            return py::none(); //  shared_ptr<CoefficientFunction>();
          }))

    
    .def_property_readonly("derivname", FunctionPointer
                  ([](PyGF self) -> string
                   {
                     auto deriv = self->GetFESpace()->GetFluxEvaluator();
                     if (!deriv) return "";
                     return deriv->Name();
                   }))

    .def("__call__", FunctionPointer
         ([](PyGF self, double x, double y, double z)
          {
            auto space = self->GetFESpace();
            auto evaluator = space->GetEvaluator();
            LocalHeap lh(10000, "ngcomp::GridFunction::Eval");

            IntegrationPoint ip;
            int elnr = space->GetMeshAccess()->FindElementOfPoint(Vec<3>(x, y, z), ip, true);
            if (elnr < 0) throw Exception ("point out of domain");
            ElementId ei(VOL, elnr);
            
            const FiniteElement & fel = space->GetFE(ei, lh);

            Array<int> dnums(fel.GetNDof(), lh);
            space->GetDofNrs(ei, dnums);
            auto & trafo = space->GetMeshAccess()->GetTrafo(ei, lh);

            if (space->IsComplex())
              {
                Vector<Complex> elvec(fel.GetNDof()*space->GetDimension());
                Vector<Complex> values(evaluator->Dim());
                self->GetElementVector(dnums, elvec);

                evaluator->Apply(fel, trafo(ip, lh), elvec, values, lh);
                return (values.Size() > 1) ? py::cast(values) : py::cast(values(0));
              }
            else
              {
                Vector<> elvec(fel.GetNDof()*space->GetDimension());
                Vector<> values(evaluator->Dim());
                self->GetElementVector(dnums, elvec);

                evaluator->Apply(fel, trafo(ip, lh), elvec, values, lh);
                return (values.Size() > 1) ? py::cast(values) : py::cast(values(0));
              }
          }
          ), py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0)


   .def("__call__", FunctionPointer
        ([](PyGF self, const BaseMappedIntegrationPoint & mip)
          {
            auto space = self->GetFESpace();

            ElementId ei = mip.GetTransformation().GetElementId();
            // auto evaluator = space->GetEvaluator(ei.IsBoundary());
            auto evaluator = space->GetEvaluator(VorB(ei));
            LocalHeap lh(10000, "ngcomp::GridFunction::Eval");

            // int elnr = mip.GetTransformation().GetElementNr();
            const FiniteElement & fel = space->GetFE(ei, lh);

            Array<int> dnums(fel.GetNDof());
            space->GetDofNrs(ei, dnums);

            if (space->IsComplex())
              {
                Vector<Complex> elvec(fel.GetNDof()*space->GetDimension());
                Vector<Complex> values(evaluator->Dim());
                self->GetElementVector(dnums, elvec);

                evaluator->Apply(fel, mip, elvec, values, lh);
                return (values.Size() > 1) ? py::cast(values) : py::cast(values(0));
              }
            else
              {
                Vector<> elvec(fel.GetNDof()*space->GetDimension());
                Vector<> values(evaluator->Dim());
                self->GetElementVector(dnums, elvec);
                evaluator->Apply(fel, mip, elvec, values, lh);
                return (values.Size() > 1) ? py::cast(values) : py::cast(values(0));
              }
          }), 
        py::arg("mip"))
    

    .def("D", FunctionPointer
         ([](PyGF self, const double &x, const double &y, const double &z)
          {
            const FESpace & space = *self->GetFESpace();
            IntegrationPoint ip;
            int dim_mesh = space.GetMeshAccess()->GetDimension();
            auto evaluator = space.GetFluxEvaluator();
            cout << evaluator->Name() << endl;
            int dim = evaluator->Dim();
            LocalHeap lh(10000, "ngcomp::GridFunction::Eval");
            int elnr = space.GetMeshAccess()->FindElementOfPoint(Vec<3>(x, y, z), ip, true);
            ElementId ei(VOL, elnr);
            Array<int> dnums;
            space.GetDofNrs(ei, dnums);
            const FiniteElement & fel = space.GetFE(ei, lh);
            if (space.IsComplex())
              {
                Vector<Complex> elvec;
                Vector<Complex> values(dim);
                elvec.SetSize(fel.GetNDof());
                self->GetElementVector(dnums, elvec);
                if (dim_mesh == 2)
                  {
                    MappedIntegrationPoint<2, 2> mip(ip, space.GetMeshAccess()->GetTrafo(ei, lh));
                    evaluator->Apply(fel, mip, elvec, values, lh);
                  }
                else if (dim_mesh == 3)
                  {
                    MappedIntegrationPoint<3, 3> mip(ip, space.GetMeshAccess()->GetTrafo(ei, lh));
                    evaluator->Apply(fel, mip, elvec, values, lh);
                  }
                if (dim > 1)
                  return py::cast(values);
                else
                  return py::cast(values(0));
              }
            else
              {
                Vector<> elvec;
                Vector<> values(dim);
                elvec.SetSize(fel.GetNDof());
                self->GetElementVector(dnums, elvec);
                ElementId ei(VOL, elnr);
                if (dim_mesh == 2)
                  {
                    MappedIntegrationPoint<2, 2> mip(ip, space.GetMeshAccess()->GetTrafo(ei, lh));
                    evaluator->Apply(fel, mip, elvec, values, lh);
                  }
                else if (dim_mesh == 3)
                  {
                    MappedIntegrationPoint<3, 3> mip(ip, space.GetMeshAccess()->GetTrafo(ei, lh));
                    evaluator->Apply(fel, mip, elvec, values, lh);
                  }
                if (dim > 1)
                  return py::cast(values);
                else
                  return py::cast(values(0));
              }
          }
          ), py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0)


    .def("CF", FunctionPointer
         ([](PyGF self, shared_ptr<DifferentialOperator> diffop) -> PyCF
          {
            if (!diffop->Boundary())
              return PyCF(make_shared<GridFunctionCoefficientFunction> (self.Get(), diffop));
            else
              return PyCF(make_shared<GridFunctionCoefficientFunction> (self.Get(), nullptr, diffop));
          }))
    
    ;



  //////////////////////////////////////////////////////////////////////////////////////////

//   PyExportArray<shared_ptr<BilinearFormIntegrator>> ();

  // typedef BilinearForm BF;
  typedef PyWrapper<BilinearForm> PyBF;
  py::class_<PyBF>(m, "BilinearForm")
    .def("__init__",
         [](PyBF *instance, PyFES fespace, string name, 
                              bool symmetric, py::dict bpflags)
                           {
                             Flags flags = py::extract<Flags> (bpflags)();
                             if (symmetric) flags.SetFlag("symmetric");
                             new (instance) PyBF(CreateBilinearForm (fespace.Get(), name, flags));
                           },
           py::arg("space"),
           py::arg("name")="bfa", 
           py::arg("symmetric") = false,
           py::arg("flags") = py::dict())
    
    .def("__init__",
         [](PyBF *instance, PyFES trial_space, PyFES test_space,
                              string name, py::dict bpflags)
                           {
                             Flags flags = py::extract<Flags> (bpflags)();
                             new (instance) PyBF(CreateBilinearForm (trial_space.Get(), test_space.Get(), name, flags));
                           },
           py::arg("trialspace"),
           py::arg("testspace"),
           py::arg("name")="bfa", 
           py::arg("flags") = py::dict())

    .def("__str__", FunctionPointer( []( PyBF & self ) { return ToString<BilinearForm>(*self.Get()); } ))

    .def("Add", FunctionPointer ([](PyBF & self, PyWrapper<BilinearFormIntegrator> bfi) -> PyBF&
                                 { self->AddIntegrator (bfi.Get()); return self; }),
         py::return_value_policy::reference,
         "add integrator to bilinear-form")
    
    .def("__iadd__",FunctionPointer
                  ([](PyBF self, PyWrapper<BilinearFormIntegrator> other) { *self += other.Get(); return self; } ))

    .def_property_readonly("integrators", FunctionPointer
                  ([](PyBF & self)
                   {
                     py::list igts;
                     for (auto igt : self->Integrators())
                       igts.append (py::cast(PyWrapper<BilinearFormIntegrator> (igt)));
                     return igts;
                   } ))
    
    .def("Assemble", FunctionPointer([](PyBF & self, int heapsize, bool reallocate)
                                     {
                                       if (heapsize > global_heapsize)
                                         {
                                           global_heapsize = heapsize;
                                           glh = LocalHeap(heapsize, "python-comp lh", true);
                                         }
                                       self->ReAssemble(glh,reallocate);
                                     }),
         py::arg("heapsize")=1000000,py::arg("reallocate")=false)

    .def_property_readonly("mat", FunctionPointer([](PyBF & self) -> PyBaseMatrix
                                         {
                                           auto mat = self->GetMatrixPtr();
                                           if (!mat)
                                             throw py::type_error("matrix not ready - assemble bilinearform first");
                                           return mat;
                                         }))

    .def("__getitem__", FunctionPointer( [](PyBF & self, py::tuple t)
                                         {
                                           int ind1 = py::extract<int>(t[0])();
                                           int ind2 = py::extract<int>(t[1])();
                                           cout << "get bf, ind = " << ind1 << "," << ind2 << endl;
                                         }))
    

    .def_property_readonly("components", FunctionPointer
                  ([](PyBF & self)-> py::list
                   { 
                     py::list bfs;
                     auto fes = dynamic_pointer_cast<CompoundFESpace> (self->GetFESpace());
                     if (!fes)
                       throw py::type_error("not a compound-fespace\n");
                       
                     int ncomp = fes->GetNSpaces();
                     for (int i = 0; i < ncomp; i++)
                       // bfs.append(shared_ptr<BilinearForm> (new ComponentBilinearForm(self.Get().get(), i, ncomp)));
                       bfs.append(py::cast(PyWrapper<BilinearForm> (make_shared<ComponentBilinearForm>(self.Get(), i, ncomp))));
                     return bfs;
                   }),
                  "list of components for bilinearforms on compound-space")

    .def("__call__", FunctionPointer
         ([](PyBF & self, const GridFunction & u, const GridFunction & v)
          {
            auto au = self->GetMatrix().CreateVector();
            au = self->GetMatrix() * u.GetVector();
            return InnerProduct (au, v.GetVector());
          }))

    .def("Energy",FunctionPointer
         ([](PyBF & self, PyBaseVector & x)
          {
            return self->Energy(*x);
          }))
    
    .def("Apply", FunctionPointer
	 ([](PyBF & self, PyBaseVector & x, PyBaseVector & y, int heapsize)
	  {
            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
	    self->ApplyMatrix (*x, *y, glh);
	  }),
         py::arg("x"),py::arg("y"),py::arg("heapsize")=1000000)

    .def("ComputeInternal", FunctionPointer
	 ([](PyBF & self, PyBaseVector & u, PyBaseVector & f, int heapsize)
	  {
            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
	    self->ComputeInternal (*u, *f, glh );
	  }),
         py::arg("u"),py::arg("f"),py::arg("heapsize")=1000000)

    .def("AssembleLinearization", FunctionPointer
	 ([](PyBF & self, PyBaseVector & ulin, int heapsize)
	  {
            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
	    self->AssembleLinearization (*ulin, glh);
	  }),
         py::arg("ulin"),py::arg("heapsize")=1000000)

    .def("Flux", FunctionPointer
         ([](PyBF & self, shared_ptr<GridFunction> gf) -> PyCF
          {
            return PyCF(make_shared<GridFunctionCoefficientFunction> (gf, self->GetIntegrator(0)));
          }))
    
    .def_property_readonly("harmonic_extension", FunctionPointer
                  ([](PyBF & self) -> PyBaseMatrix
                   {
                     return self->GetHarmonicExtension();
                   })
                  )
    .def_property_readonly("harmonic_extension_trans", FunctionPointer
                  ([](PyBF & self) -> PyBaseMatrix
                   {
                     return self->GetHarmonicExtensionTrans();
                   })
                  )
    .def_property_readonly("inner_solve", FunctionPointer
                  ([](PyBF & self) -> PyBaseMatrix
                   {
                     return self->GetInnerSolve();
                   })
                  )
    ;

  //////////////////////////////////////////////////////////////////////////////////////////

//   PyExportArray<shared_ptr<LinearFormIntegrator>> ();
// 
  // typedef LinearForm LF;
  typedef PyWrapper<LinearForm> PyLF;
  py::class_<PyLF>(m, "LinearForm")
    .def("__init__",
         [](PyLF *instance, PyFES fespace, string name, Flags flags) // -> shared_ptr<LinearForm>
                           {
                             auto f = CreateLinearForm (fespace.Get(), name, flags);
                             f->AllocateVector();
                             new (instance) PyLF(f);
                           },
          py::arg("space"), py::arg("name")="lff", py::arg("flags") = py::dict()
         )
    .def("__str__", FunctionPointer( []( PyLF & self ) { return ToString<LinearForm>(*self); } ))

    .def_property_readonly("vec", FunctionPointer([] (PyLF self) -> PyBaseVector { return self->GetVectorPtr();}))

    .def("Add", FunctionPointer
         ([](PyLF self, PyWrapper<LinearFormIntegrator> lfi)
          { 
            self->AddIntegrator (lfi.Get());
            return self; 
          }),
         py::arg("integrator"))

    .def("__iadd__",FunctionPointer
                  ([](PyLF self, PyWrapper<LinearFormIntegrator> & other) { *self+=other.Get(); return self; } ))


    .def_property_readonly("integrators", FunctionPointer
                  ([](PyLF self)
                   {
                     py::list igts;
                     for (auto igt : self->Integrators())
                       igts.append (py::cast(PyWrapper<LinearFormIntegrator> (igt)));
                     return igts;
                   } ))

    .def("Assemble", FunctionPointer
         ([](PyLF self, int heapsize)
          { 
            if (heapsize > global_heapsize)
              {
                global_heapsize = heapsize;
                glh = LocalHeap(heapsize, "python-comp lh", true);
              }
            self->Assemble(glh);
          }),
         py::arg("heapsize")=1000000)

    .def_property_readonly("components", FunctionPointer
                  ([](PyLF self)-> py::list
                   { 
                     py::list lfs;
                     auto fes = dynamic_pointer_cast<CompoundFESpace> (self->GetFESpace());
                     if (!fes)
                       throw py::type_error("not a compound-fespace\n");
                       
                     int ncomp = fes->GetNSpaces();
                     for (int i = 0; i < ncomp; i++)
                       // lfs.append(shared_ptr<LinearForm> (new ComponentLinearForm(self.Get().get(), i, ncomp)));
                       lfs.append(py::cast(PyWrapper<LinearForm> (make_shared<ComponentLinearForm>(self.Get(), i, ncomp))));
                     return lfs;
                   }),
                  "list of components for linearforms on compound-space")
    
    .def("__call__", FunctionPointer
         ([](PyLF self, const GridFunction & v)
          {
            return InnerProduct (self->GetVector(), v.GetVector());
          }))

    ;

  //////////////////////////////////////////////////////////////////////////////////////////

  py::class_<Preconditioner, shared_ptr<Preconditioner>, BaseMatrix>(m, "CPreconditioner")
    .def ("Update", [](Preconditioner &pre) { pre.Update();} )
    .def_property_readonly("mat", FunctionPointer
                  ([](Preconditioner &self) -> PyWrapper<BaseMatrix>
                   {
                     return self.GetMatrixPtr();
                     /*
                     return shared_ptr<BaseMatrix> (const_cast<BaseMatrix*> (&self.GetMatrix()),
                                                    NOOP_Deleter);
                     */
                   }))
    ;

   
   m.def("Preconditioner",
         [](PyWrapper<BilinearForm> bfa, const string & type, Flags flags)
                           { 
                             auto creator = GetPreconditionerClasses().GetPreconditioner(type);
                             if (creator == nullptr)
                               throw Exception(string("nothing known about preconditioner '") + type + "'");
                             return creator->creatorbf(bfa.Get(), flags, "noname-pre");
                           },
          py::arg("bf"), py::arg("type"), py::arg("flags")=py::dict()
          );


  //////////////////////////////////////////////////////////////////////////////////////////

  py::class_<NumProc, NGS_Object, shared_ptr<NumProc>> (m, "NumProc")
    .def("Do", FunctionPointer([](NumProc & self, int heapsize)
                               {
                                 LocalHeap lh (heapsize, "NumProc::Do-heap");
                                 self.Do(lh);
                               }),
         py::arg("heapsize")=1000000)
    ;

//   // die geht
//   py::class_<NumProcWrap,shared_ptr<NumProcWrap>, NumProc>("PyNumProc", py::init<shared_ptr<PDE>, const Flags&>())
//     .def("Do", py::pure_virtual(&PyNumProc::Do)) 
//     .def_property_readonly("pde", &PyNumProc::GetPDE)
//     ;
//   
//   py::implicitly_convertible 
//     <shared_ptr<NumProcWrap>, shared_ptr<NumProc> >(); 
// 

  //////////////////////////////////////////////////////////////////////////////////////////

  PyExportSymbolTable<shared_ptr<FESpace>, PyWrapper<FESpace>> (m);
  PyExportSymbolTable<shared_ptr<CoefficientFunction>, PyWrapper<CoefficientFunction>> (m);
  PyExportSymbolTable<shared_ptr<GridFunction>, PyWrapper<GridFunction>> (m);
  PyExportSymbolTable<shared_ptr<BilinearForm>, PyWrapper<BilinearForm>>(m);
  PyExportSymbolTable<shared_ptr<LinearForm>, PyWrapper<LinearForm>>(m);
  PyExportSymbolTable<shared_ptr<Preconditioner>, PyWrapper<Preconditioner>> (m);
  PyExportSymbolTable<shared_ptr<NumProc>, PyWrapper<NumProc>> (m);
  PyExportSymbolTable<double> (m);
  PyExportSymbolTable<shared_ptr<double>> (m);

  typedef PyWrapper<PDE> PyPDE;
  py::class_<PyPDE> (m, "PDE")

    // .def(py::init<const string&>())
    .def(py::init<>())
    

#ifndef PARALLEL
    .def("__init__",
         [](PyPDE *instance, const string & filename)
                           { 
                             new (instance) PyPDE(LoadPDE (filename));
                           },
          py::arg("filename")
          )

#else

    .def("__init__",
         [](PyPDE *instance, const string & filename,
                              py::object py_mpicomm)
                           { 
                             PyObject * py_mpicomm_ptr = py_mpicomm.ptr();
                             if (py_mpicomm_ptr != Py_None)
                               {
                                 MPI_Comm * comm = PyMPIComm_Get (py_mpicomm_ptr);
                                 ngs_comm = *comm;
                               }
                             else
                               ngs_comm = MPI_COMM_WORLD;

                             cout << "Rank = " << MyMPI_GetId(ngs_comm) << "/"
                                  << MyMPI_GetNTasks(ngs_comm) << endl;

                             NGSOStream::SetGlobalActive (MyMPI_GetId()==0);
                             new (instance) PyPDE(LoadPDE (filename));
                           },
          py::arg("filename"), py::arg("mpicomm")=DummyArgument()
          )
#endif



    .def("LoadSolution", []( PyPDE self, string filename, bool ascii )
        {
          return self->LoadSolution(filename, ascii);
        },
         py::arg("filename"), py::arg("ascii")=false
      )

    
    /*
    .def("Load", 
         // static_cast<void(PDE::*)(const string &, const bool, const bool)> 
         // (&PDE::LoadPDE),
         FunctionPointer ([](shared_ptr<PDE> pde, const string & filename)
                          { 
                            LoadPDE (pde, filename);
                          }))
    */

    // .def("__str__", &ToString<PDE>)
    .def("__str__", [] (PyPDE & self) { return ToString(*self.Get()); } )

    .def("Mesh",  [](PyPDE self, int meshnr)
        {
          return self->GetMeshAccess(meshnr);
        },
       py::arg("meshnr")=0
       )

    .def("Solve", [](PyPDE self) { self->Solve(); } )


    .def("Add", FunctionPointer([](PyPDE self, shared_ptr<MeshAccess> mesh)
                                {
                                  self->AddMeshAccess (mesh);
                                }))

    .def("Add", FunctionPointer([](PyPDE self, const string & name, double val)
                                {
                                  self->AddConstant (name, val);
                                }))

    .def("Add", FunctionPointer([](PyPDE self, PyWrapper<FESpace> space)
                                {
                                  self->AddFESpace (space->GetName(), space.Get());
                                }))

    .def("Add", FunctionPointer([](PyPDE self, PyWrapper<GridFunction> gf)
                                {
                                  self->AddGridFunction (gf->GetName(), gf.Get());
                                }))

    .def("Add", FunctionPointer([](PyPDE self, PyWrapper<BilinearForm> bf)
                                {
                                  self->AddBilinearForm (bf->GetName(), bf.Get());
                                }))

    .def("Add", FunctionPointer([](PyPDE self, PyWrapper<LinearForm> lf)
                                {
                                  self->AddLinearForm (lf->GetName(), lf.Get());
                                }))

    .def("Add", FunctionPointer([](PyPDE self, shared_ptr<Preconditioner> pre)
                                {
                                  self->AddPreconditioner (pre->GetName(), pre);
                                }))

// TODO
//     .def("Add", FunctionPointer([](PyPDE self, shared_ptr<NumProcWrap> np)
//                                 {
//                                   cout << "add pynumproc" << endl;
//                                   self->AddNumProc ("pynumproc", np);
//                                 }))
    
    .def("Add", FunctionPointer([](PyPDE self, shared_ptr<NumProc> np)
                                {
				  static int cnt = 0;
				  cnt++;
				  string name = "np_from_py" + ToString(cnt);
                                  self->AddNumProc (name, np);
                                }))

    .def("Add", FunctionPointer([](PyPDE self, const py::list &l)
                                {
                                  for (int i=0; i<py::len(l); i++)
                                    {
                                      py::extract<shared_ptr<PyNumProc>> np(l[i]);
                                      if(np.check())
                                        {
                                          self->AddNumProc (np()->GetName(), np());
                                          continue;
                                        }
                                      
                                      py::extract<shared_ptr<NumProc>> pnp(l[i]);
                                      if(np.check())
                                        {
                                          self->AddNumProc (pnp()->GetName(), pnp());
                                          continue;
                                        }
                                      
                                      py::extract<shared_ptr<GridFunction>> gf(l[i]);
                                      if(gf.check())
                                        {
                                          self->AddGridFunction (gf()->GetName(), gf());
                                          continue;
                                        }
                                      
                                      py::extract<shared_ptr<BilinearForm>> bf(l[i]);
                                      if(gf.check())
                                        {
                                          self->AddBilinearForm (bf()->GetName(), bf());
                                          continue;
                                        }
                                      
                                      py::extract<shared_ptr<LinearForm>> lf(l[i]);
                                      if(gf.check())
                                        {
                                          self->AddLinearForm (lf()->GetName(), lf());
                                          continue;
                                        }
                                      
                                      py::extract<shared_ptr<Preconditioner>> pre(l[i]);
                                      if(gf.check())
                                        {
                                          self->AddPreconditioner (pre()->GetName(), pre());
                                          continue;
                                        }
                                      
                                      cout << "warning: unknown object at position " << i << endl;
                                    }
                                }))

    .def("SetCurveIntegrator", FunctionPointer
         ([](PyPDE self, const string & filename, PyWrapper<LinearFormIntegrator> lfi)
          {
            self->SetLineIntegratorCurvePointInfo(filename, lfi.Get().get());
          }))

    .def_property_readonly ("constants", FunctionPointer([](PyPDE self) { return py::cast(self->GetConstantTable()); }))
    .def_property_readonly ("variables", FunctionPointer([](PyPDE self) { return py::cast(self->GetVariableTable()); }))
    .def_property_readonly ("coefficients", FunctionPointer([](PyPDE self) { return py::cast(self->GetCoefficientTable()); }))
    .def_property_readonly ("spaces", FunctionPointer([](PyPDE self) {
          auto table = self->GetSpaceTable();
          SymbolTable<shared_ptr<FESpace>> pytable;
          for ( auto i : Range(table.Size() ))
                pytable.Set(table.GetName(i), shared_ptr<FESpace>(table[i]));
          return py::cast(pytable);
          }))
    .def_property_readonly ("gridfunctions", FunctionPointer([](PyPDE self) {
          auto table = self->GetGridFunctionTable();
          SymbolTable<shared_ptr<GridFunction>> pytable;
          for ( auto i : Range(table.Size() ))
                pytable.Set(table.GetName(i), shared_ptr<GridFunction>(table[i]));
          return py::cast(pytable);
          }))
    .def_property_readonly ("bilinearforms", FunctionPointer([](PyPDE self) {
          auto table = self->GetBilinearFormTable();
          SymbolTable<shared_ptr<BilinearForm>> pytable;
          for ( auto i : Range(table.Size() ))
                pytable.Set(table.GetName(i), shared_ptr<BilinearForm>(table[i]));
          return py::cast(pytable);
          }))
    .def_property_readonly ("linearforms", FunctionPointer([](PyPDE self) {
          auto table = self->GetLinearFormTable();
          SymbolTable<shared_ptr<LinearForm>> pytable;
          for ( auto i : Range(table.Size() ))
                pytable.Set(table.GetName(i), shared_ptr<LinearForm>(table[i]));
          return py::cast(pytable);
          }))
    .def_property_readonly ("preconditioners", FunctionPointer([](PyPDE self) { return py::cast(self->GetPreconditionerTable()); }))
    .def_property_readonly ("numprocs", FunctionPointer([](PyPDE self) { return py::cast(self->GetNumProcTable()); }))
    ;
  
  m.def("Integrate", 
          [](PyCF cf,
                             shared_ptr<MeshAccess> ma, 
	     VorB vb, int order, py::object definedon,
	     bool region_wise, bool element_wise, int heapsize)
                          {
                            static Timer t("Integrate CF"); RegionTimer reg(t);
                            // static mutex addcomplex_mutex;
			    if (heapsize > global_heapsize)
			      {
				global_heapsize = heapsize;
				glh = LocalHeap(heapsize, "python-comp lh", true);
			      }
                           py::extract<Region> defon_region(definedon);
                           if (defon_region.check())
                             vb = VorB(defon_region());
                           BitArray mask(ma->GetNRegions(vb));
                           mask.Set();
                           if(defon_region.check())
                             for(auto i : Range(ma->GetNRegions(vb)))
                               if(!defon_region().Mask().Test(i))
                                 mask.Clear(i);
			   int dim = cf.Get()->Dimension();
			   if((region_wise || element_wise) && dim != 1)
			     throw Exception("region_wise and element_wise only implemented for 1 dimensional coefficientfunctions");

                            if (!cf->IsComplex())
                              {
                                Vector<> sum(dim);
				sum = 0.0;
			        Vector<> region_sum(region_wise ? ma->GetNRegions(vb) : 0);
                                Vector<> element_sum(element_wise ? ma->GetNE(vb) : 0);
                                region_sum = 0;
                                element_sum = 0;
				bool use_simd = true;
				
                                
                                ma->IterateElements
                                  (vb, glh, [&] (Ngs_Element el, LocalHeap & lh)
                                   {
				     if(!mask.Test(el.GetIndex())) return;
                                     auto & trafo = ma->GetTrafo (el, lh);
                                     FlatVector<> hsum(dim, lh);
				     hsum = 0.0;
                                     bool this_simd = use_simd;
                                     
                                     if (this_simd)
                                       {
                                         try
                                           {
                                             SIMD_IntegrationRule ir(trafo.GetElementType(), order);
                                             auto & mir = trafo(ir, lh);
                                             FlatMatrix<SIMD<double>> values(dim,ir.Size(), lh);
                                             cf.Get() -> Evaluate (mir, values);
                                             FlatVector<SIMD<double>> vsum(dim, lh);
					     vsum = 0;
                                             for (size_t j = 0; j < dim; j++)
                                               for (size_t i = 0; i < values.Width(); i++)
                                                 vsum(j) += mir[i].GetWeight() * values(j,i);
					     for(int i = 0; i< dim; i++)
					       hsum[i] = HSum(vsum[i]);
                                           }
                                         catch (ExceptionNOSIMD e)
                                           {
                                             this_simd = false;
                                             use_simd = false;
                                             hsum = 0.0;
                                           }
                                       }
                                     if (!this_simd)
                                       {
                                         IntegrationRule ir(trafo.GetElementType(), order);
                                         BaseMappedIntegrationRule & mir = trafo(ir, lh);
                                         FlatMatrix<> values(ir.Size(), dim, lh);
                                         cf.Get() -> Evaluate (mir, values);
                                         for (int i = 0; i < values.Height(); i++)
                                           hsum += mir[i].GetWeight() * values.Row(i);
                                       }
				     for(size_t i = 0; i<dim;i++)
				       AsAtomic(sum(i)) += hsum(i);
				     if(region_wise)
				       AsAtomic(region_sum(el.GetIndex())) += hsum(0);
                                     if (element_wise)
                                       element_sum(el.Nr()) = hsum(0);
                                   });
                                py::object result;
                                if (region_wise)
                                  result = py::list(py::cast(region_sum));
                                else if (element_wise)
                                  result = py::cast(element_sum);
                                else if(dim==1)
				    result = py::cast(sum(0));
                                else
                                  result = py::cast(sum);
                                return result;
                              }
                            else
                              {
                                Vector<Complex> sum(dim);
				sum = 0.0;
                                Vector<Complex> region_sum(region_wise ? ma->GetNRegions(vb) : 0);
                                Vector<Complex> element_sum(element_wise ? ma->GetNE(vb) : 0);
                                region_sum = 0;
                                element_sum = 0;

                                bool use_simd = true;
                                
                                ma->IterateElements
                                  (vb, glh, [&] (Ngs_Element el, LocalHeap & lh)
                                   {
				     if(!mask.Test(el.GetIndex())) return;
                                     auto & trafo = ma->GetTrafo (el, lh);
                                     Vector<Complex> hsum(dim);
				     hsum = 0.0;
                                     
                                     bool this_simd = use_simd;

                                     if (this_simd)
                                       {
                                         try
                                           {
                                             SIMD_IntegrationRule ir(trafo.GetElementType(), order);
                                             auto & mir = trafo(ir, lh);
                                             FlatMatrix<SIMD<Complex>> values(dim, ir.Size(), lh);
                                             cf.Get() -> Evaluate (mir, values);
                                             FlatVector<SIMD<Complex>> vsum(dim,lh);
					     vsum = Complex(0.0);
                                             for (size_t j = 0; j < dim; j++)
                                               for (size_t i = 0; i < values.Width(); i++)
                                                 vsum(j) += mir[i].GetWeight() * values(j,i);
					     for(size_t i = 0; i < dim; i++)
					       hsum[i] = HSum(vsum[i]);
                                           }
                                         catch (ExceptionNOSIMD e)
                                           {
                                             this_simd = false;
                                             use_simd = false;
                                             hsum = 0.0;
                                           }
                                       }
                                     if (!this_simd)
                                       {
                                         IntegrationRule ir(trafo.GetElementType(), order);
                                         BaseMappedIntegrationRule & mir = trafo(ir, lh);
                                         FlatMatrix<Complex> values(ir.Size(), dim, lh);
                                         cf.Get() -> Evaluate (mir, values);
                                         for (int i = 0; i < values.Height(); i++)
                                           hsum += mir[i].GetWeight() * values.Row(i);
                                       }
                                     for(size_t i = 0; i<dim; i++)
				       MyAtomicAdd (sum(i), hsum(i));
				     if(region_wise)
				       MyAtomicAdd (region_sum(el.GetIndex()), hsum(0));
                                     if (element_wise)
                                       element_sum(el.Nr()) = hsum(0);
                                   });
                                
                                py::object result;
                                if (region_wise)
                                  result = py::list(py::cast(region_sum));
                                else if (element_wise)
                                  result = py::cast(element_sum);
                                else if(dim==1)
				  result = py::cast(sum(0));
				else
                                  result = py::cast(sum);
                                return result;
                              }
                          },
           py::arg("cf"), py::arg("mesh"), py::arg("VOL_or_BND")=VOL, 
           py::arg("order")=5,
	py::arg("definedon")=DummyArgument(),
           py::arg("region_wise")=false,
	py::arg("element_wise")=false,
	py::arg("heapsize") = 1000000)
    ;
  






  m.def("SymbolicLFI",
          [](PyCF cf, VorB vb, bool element_boundary,
              bool skeleton, py::object definedon) 
           {
             py::extract<Region> defon_region(definedon);
             if (defon_region.check())
               vb = VorB(defon_region());

             shared_ptr<LinearFormIntegrator> lfi;
             if (!skeleton)
               lfi = make_shared<SymbolicLinearFormIntegrator> (cf.Get(), vb, element_boundary);
             else
               lfi = make_shared<SymbolicFacetLinearFormIntegrator> (cf.Get(), vb /* , element_boundary */);
             
             if (py::extract<py::list> (definedon).check())
               {
                 cout << "warning: SymbolicLFI definedon changed to 1-based" << endl;
                 Array<int> defon = makeCArray<int> (definedon);
                 for (int & d : defon) d--;
                 lfi -> SetDefinedOn (defon); 
               }
               
             // lfi -> SetDefinedOn (makeCArray<int> (definedon));

             if (defon_region.check())
               lfi->SetDefinedOn(defon_region().Mask());

             return PyWrapper<LinearFormIntegrator>(lfi);
           },
           py::arg("form"),
           py::arg("VOL_or_BND")=VOL,
           py::arg("element_boundary")=false,
           py::arg("skeleton")=false,           
           py::arg("definedon")=DummyArgument()
          );

  m.def("SymbolicBFI",
          [](PyCF cf, VorB vb, bool element_boundary,
             bool skeleton, py::object definedon,
             IntegrationRule ir)
           {
             py::extract<Region> defon_region(definedon);
             if (defon_region.check())
               vb = VorB(defon_region());

             // check for DG terms
             bool has_other = false;
             cf->TraverseTree ([&has_other] (CoefficientFunction & cf)
                               {
                                 if (dynamic_cast<ProxyFunction*> (&cf))
                                   if (dynamic_cast<ProxyFunction&> (cf).IsOther())
                                     has_other = true;
                               });
             if (has_other && !element_boundary && !skeleton)
               throw Exception("DG-facet terms need either skeleton=True or element_boundary=True");
             
             shared_ptr<BilinearFormIntegrator> bfi;
             if (!has_other && !skeleton)
               bfi = make_shared<SymbolicBilinearFormIntegrator> (cf.Get(), vb, element_boundary);
             else
               bfi = make_shared<SymbolicFacetBilinearFormIntegrator> (cf.Get(), vb, element_boundary);
             
             if (py::extract<py::list> (definedon).check())
               {
                 cout << "warning: SymbolicBFI definedon changed to 1-based" << endl;
                 Array<int> defon = makeCArray<int> (definedon);
                 for (int & d : defon) d--;
                 bfi -> SetDefinedOn (defon); 
               }
             // bfi -> SetDefinedOn (makeCArray<int> (definedon));

             if (defon_region.check())
               bfi->SetDefinedOn(defon_region().Mask());

             if (ir.Size())
               {
                 cout << "ir = " << ir << endl;
                 dynamic_pointer_cast<SymbolicBilinearFormIntegrator> (bfi)
                   ->SetIntegrationRule(ir);
               }
             
             return PyWrapper<BilinearFormIntegrator>(bfi);
           },
        py::arg("form"), py::arg("VOL_or_BND")=VOL,
        py::arg("element_boundary")=false,
        py::arg("skeleton")=false,
        py::arg("definedon")=DummyArgument(),
        py::arg("intrule")=IntegrationRule()
        );
          
  m.def("SymbolicTPBFI",
          [](PyCF cf, VorB vb, bool element_boundary,
              bool skeleton, py::object definedon)
           {
             py::extract<Region> defon_region(definedon);
             if (defon_region.check())
               vb = VorB(defon_region());

             // check for DG terms
             bool has_other = false;
             cf->TraverseTree ([&has_other] (CoefficientFunction & cf)
                               {
                                 if (dynamic_cast<ProxyFunction*> (&cf))
                                   if (dynamic_cast<ProxyFunction&> (cf).IsOther())
                                     has_other = true;
                               });
             if (has_other && !element_boundary && !skeleton)
               throw Exception("DG-facet terms need either skeleton=True or element_boundary=True");
             
             shared_ptr<BilinearFormIntegrator> bfi;
             if (!has_other && !skeleton)
               bfi = make_shared<TensorProductBilinearFormIntegrator> (cf.Get(), vb, element_boundary);
             else
               bfi = make_shared<TensorProductFacetBilinearFormIntegrator> (cf.Get(), vb, element_boundary);
             
             if (py::extract<py::list> (definedon).check())
               bfi -> SetDefinedOn (makeCArray<int> (definedon));

             if (defon_region.check())
               {
                 cout << IM(3) << "defineon = " << defon_region().Mask() << endl;
                 bfi->SetDefinedOn(defon_region().Mask());
               }
             
             return PyWrapper<BilinearFormIntegrator>(bfi);
           },
           py::arg("form"), py::arg("VOL_or_BND")=VOL,
           py::arg("element_boundary")=false,
           py::arg("skeleton")=false,
           py::arg("definedon")=DummyArgument()
          );
          
  m.def("SymbolicEnergy",
          [](PyCF cf, VorB vb, py::object definedon) -> PyWrapper<BilinearFormIntegrator>
           {
             py::extract<Region> defon_region(definedon);
             if (defon_region.check())
               vb = VorB(defon_region());

             auto bfi = make_shared<SymbolicEnergy> (cf.Get(), vb);
             
             if (defon_region.check())
               {
                 cout << IM(3) << "defineon = " << defon_region().Mask() << endl;
                 bfi->SetDefinedOn(defon_region().Mask());
               }
             
             /*
             py::extract<py::list> defon_list(definedon);
             if (defon_list.check())
               {
                 BitArray bits(py::len (defon_list));
                 bits.Clear();
                 bool all_booleans = true;
                 for (int i : Range(bits))
                   {
                     cout << "class = " << defon_list().attr("__class__") << endl;
                     py::extract<bool> extbool(defon_list()[i]);
                     if (extbool.check())
                       {
                         if (extbool()) bits.Set(i);
                       }
                     else
                       all_booleans = false;
                   }
                 cout << "bits: " << bits << endl;
                 cout << "allbool = " << all_booleans << endl;
               }
             */
             return PyWrapper<BilinearFormIntegrator>(bfi);
           },
           py::arg("coefficient"), py::arg("VOL_or_BND")=VOL, py::arg("definedon")=DummyArgument()
          );


  /*
  m.def("IntegrateLF", 
          FunctionPointer
          ([](shared_ptr<LinearForm> lf, 
              shared_ptr<CoefficientFunction> cf)
           {
             lf->AllocateVector();
             lf->GetVector() = 0.0;

             Array<ProxyFunction*> proxies;
             cf->TraverseTree( [&] (CoefficientFunction & nodecf)
                               {
                                 auto proxy = dynamic_cast<ProxyFunction*> (&nodecf);
                                 if (proxy && !proxies.Contains(proxy))
                                   proxies.Append (proxy);
                               });
             
             LocalHeap lh1(1000000, "lh-Integrate");

             // for (auto el : lf->GetFESpace()->Elements(VOL, lh))
             IterateElements 
               (*lf->GetFESpace(), VOL, lh1,
                [&] (FESpace::Element el, LocalHeap & lh)
               {
                 const FiniteElement & fel = el.GetFE();
                 auto & trafo = lf->GetMeshAccess()->GetTrafo (el, lh);
                 IntegrationRule ir(trafo.GetElementType(), 2*fel.Order());
                 BaseMappedIntegrationRule & mir = trafo(ir, lh);
                 FlatVector<> elvec(fel.GetNDof(), lh);
                 FlatVector<> elvec1(fel.GetNDof(), lh);

                 FlatMatrix<> values(ir.Size(), cf->Dimension(), lh);
                 ProxyUserData ud;
                 trafo.userdata = &ud;

                 elvec = 0;
                 for (auto proxy : proxies)
                   {
                     FlatMatrix<> proxyvalues(ir.Size(), proxy->Dimension(), lh);
                     for (int k = 0; k < proxy->Dimension(); k++)
                       {
                         ud.testfunction = proxy;
                         ud.test_comp = k;
                         
                         cf -> Evaluate (mir, values);
                         for (int i = 0; i < mir.Size(); i++)
                           values.Row(i) *= mir[i].GetWeight();
                         proxyvalues.Col(k) = values.Col(0);
                       }

                     proxy->Evaluator()->ApplyTrans(fel, mir, proxyvalues, elvec1, lh);
                     elvec += elvec1;
                   }
                 lf->AddElementVector (el.GetDofs(), elvec);
               });
           }));
           


  m.def("IntegrateBF", 
          FunctionPointer
          ([](shared_ptr<BilinearForm> bf1, 
              shared_ptr<CoefficientFunction> cf)
           {
             auto bf = dynamic_pointer_cast<S_BilinearForm<double>> (bf1);
             bf->GetMatrix().SetZero();

             Array<ProxyFunction*> trial_proxies, test_proxies;
             cf->TraverseTree( [&] (CoefficientFunction & nodecf)
                               {
                                 auto proxy = dynamic_cast<ProxyFunction*> (&nodecf);
                                 if (proxy) 
                                   {
                                     if (proxy->IsTestFunction())
                                       {
                                         if (!test_proxies.Contains(proxy))
                                           test_proxies.Append (proxy);
                                       }
                                     else
                                       {                                         
                                         if (!trial_proxies.Contains(proxy))
                                           trial_proxies.Append (proxy);
                                       }
                                   }
                               });

             ProxyUserData ud;
             LocalHeap lh(1000000, "lh-Integrate");

             // IterateElements (*lf->GetFESpace(), VOL, lh,
             for (auto el : bf->GetFESpace()->Elements(VOL, lh))
               {
                 const FiniteElement & fel = el.GetFE();
                 auto & trafo = bf->GetMeshAccess()->GetTrafo (el, lh);
                 trafo.userdata = &ud;
                 IntegrationRule ir(trafo.GetElementType(), 2*fel.Order());
                 BaseMappedIntegrationRule & mir = trafo(ir, lh);
                 FlatMatrix<> elmat(fel.GetNDof(), lh);

                 FlatMatrix<> values(ir.Size(), 1, lh);

                 elmat = 0;

                 for (int i = 0; i < mir.Size(); i++)
                   {
                     auto & mip = mir[i];
                     
                     for (auto proxy1 : trial_proxies)
                       for (auto proxy2 : test_proxies)
                         {
                           HeapReset hr(lh);

                           FlatMatrix<> proxyvalues(proxy2->Dimension(), 
                                                    proxy1->Dimension(), 
                                                    lh);
                           for (int k = 0; k < proxy1->Dimension(); k++)
                             for (int l = 0; l < proxy2->Dimension(); l++)
                               {
                                 ud.trialfunction = proxy1;
                                 ud.trial_comp = k;
                                 ud.testfunction = proxy2;
                                 ud.test_comp = l;
                                 proxyvalues(l,k) = 
                                   mip.GetWeight() * cf -> Evaluate (mip);
                               }
                           
                           FlatMatrix<double,ColMajor> bmat1(proxy1->Dimension(), fel.GetNDof(), lh);
                           FlatMatrix<double,ColMajor> dbmat1(proxy1->Dimension(), fel.GetNDof(), lh);
                           FlatMatrix<double,ColMajor> bmat2(proxy2->Dimension(), fel.GetNDof(), lh);

                           proxy1->Evaluator()->CalcMatrix(fel, mip, bmat1, lh);
                           proxy2->Evaluator()->CalcMatrix(fel, mip, bmat2, lh);
                           dbmat1 = proxyvalues * bmat1;
                           elmat += Trans (bmat2) * dbmat1;
                         }
                   }
                 bf->AddElementMatrix (el.GetDofs(), el.GetDofs(), elmat, el, lh);
               }
           }));
  */

  
   m.def("TensorProductFESpace", [](py::list spaces_list, const Flags & flags ) -> PyFES
            {
              //Array<shared_ptr<FESpace> > spaces = makeCArray<shared_ptr<FESpace>> (spaces_list);
              
              auto spaces = makeCArrayUnpackWrapper<PyWrapper<FESpace>> (spaces_list);
              
              if(spaces.Size() == 2)
              {
                shared_ptr<FESpace> space( new TPHighOrderFESpace( spaces, flags ) );
                return space;
              }
              else
              {
                Array<shared_ptr<FESpace>> spaces_y(spaces.Size()-1);
                for(int i=1;i<spaces.Size();i++)
                  spaces_y[i-1] = spaces[i];
                shared_ptr<FESpace> space( new TPHighOrderFESpace( spaces[0],spaces_y, flags ) );
                return space;             
              }
              });
              
   m.def("IntDv", [](PyGF gf_tp, PyGF gf_x )
            {
              static Timer tall("comp.IntDv"); RegionTimer rall(tall);
              shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(gf_tp.Get()->GetFESpace());
              LocalHeap lh(10000000,"ReduceToXSpace");
              tpfes->ReduceToXSpace(gf_tp.Get(),gf_x.Get(),lh,
              [&] (shared_ptr<FESpace> fes,const FiniteElement & fel,const ElementTransformation & trafo,FlatVector<> elvec,FlatVector<> elvec_out,LocalHeap & lh)
              {
                 const TPHighOrderFE & tpfel = dynamic_cast<const TPHighOrderFE &>(fel);
                 shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(fes);
                 FlatMatrix<> elmat(tpfel.elements[0]->GetNDof(),tpfel.elements[1]->GetNDof(),&elvec[0]);
                 IntegrationRule ir(tpfel.elements[1]->ElementType(),2*tpfel.elements[1]->Order());
                 BaseMappedIntegrationRule & mir = trafo(ir,lh);
                 FlatMatrix<> shape(tpfel.elements[1]->GetNDof(),ir.Size(),lh);
                 dynamic_cast<const BaseScalarFiniteElement *>(tpfel.elements[1])->CalcShape(ir,shape);
                 for(int s=0;s<ir.Size();s++)
                 {
                   shape.Col(s)*=mir[s].GetWeight();
                   FlatMatrix<> tempmat(elvec_out.Size(),ir.Size(),lh);
                   tempmat = elmat*shape;
                   elvec_out+=tempmat.Col(s);
                 }
              });
              });
              
   m.def("IntDv", [](PyGF gf_tp, PyGF gf_x, PyCF coef )
           {
              static Timer tall("comp.IntDv - total domain int"); RegionTimer rall(tall);
              shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(gf_tp.Get()->GetFESpace());
              LocalHeap lh(10000000,"IntDv");
              tpfes->ReduceToXSpace(gf_tp.Get(),gf_x.Get(),lh,
              [&] (shared_ptr<FESpace> fes,const FiniteElement & fel,const ElementTransformation & trafo,FlatVector<> elvec,FlatVector<> elvec_out,LocalHeap & lh)
              {
                 const TPHighOrderFE & tpfel = dynamic_cast<const TPHighOrderFE &>(fel);
                 
                 shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(fes);
                 FlatMatrix<> coefmat(tpfel.elements[0]->GetNDof(),tpfel.elements[1]->GetNDof(),&elvec[0]);
                 IntegrationRule ir(tpfel.elements[1]->ElementType(),2*tpfel.elements[1]->Order());
                 FlatMatrix<> shape(tpfel.elements[1]->GetNDof(),ir.Size(),lh);
                 dynamic_cast<const BaseScalarFiniteElement *>(tpfel.elements[1])->CalcShape(ir,shape);
                 
                 BaseMappedIntegrationRule & mir = trafo(ir,lh);
                 
                 FlatMatrixFixWidth<1> vals(mir.Size(),lh);
                 coef.Get()->Evaluate(mir, vals);
                 for(int s=0;s<ir.Size();s++)
                 {
                   shape.Col(s)*=mir[s].GetWeight()*vals(s,0);
                   elvec_out+=coefmat*shape.Col(s);
                 }
              });
              });
    
   m.def("IntDv", [](PyGF gf_tp, py::list ax0, PyCF coef) -> double
           {
             static Timer tall("comp.IntDv2 - single point"); RegionTimer rall(tall);
             Array<double> x0_help = makeCArray<double> (ax0);
             LocalHeap lh(10000000,"IntDv2");
             shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(gf_tp.Get()->GetFESpace());
             const Array<shared_ptr<FESpace> > & spaces = tpfes->Spaces(0);
             FlatVector<> x0(spaces[0]->GetSpacialDimension(),&x0_help[0]);
             IntegrationPoint ip;
             int elnr = spaces[0]->GetMeshAccess()->FindElementOfPoint(x0,ip,true);
             auto & felx = spaces[0]->GetFE(ElementId(elnr),lh);
             FlatVector<> shapex(felx.GetNDof(),lh);
             dynamic_cast<const BaseScalarFiniteElement &>(felx).CalcShape(ip,shapex);
             double val = 0.0;
             int index = tpfes->GetIndex(elnr,0);
             Array<int> dnums;
             for(int i=index;i<index+spaces[1]->GetMeshAccess()->GetNE();i++)
             {
               auto & fely = spaces[1]->GetFE(ElementId(i-index),lh);
               tpfes->GetDofNrs(i,dnums);
               int tpndof = felx.GetNDof()*fely.GetNDof();
               FlatVector<> elvec(tpndof,lh);
               gf_tp->GetElementVector(dnums,elvec);
               FlatMatrix<> coefmat(felx.GetNDof(),fely.GetNDof(), &elvec(0));
               FlatVector<> coefy(fely.GetNDof(),lh);
               coefy = Trans(coefmat)*shapex;
               const IntegrationRule & ir = SelectIntegrationRule(fely.ElementType(),2*fely.Order());
               BaseMappedIntegrationRule & mir = spaces[1]->GetMeshAccess()->GetTrafo(ElementId(i-index),lh)(ir,lh);
               FlatMatrixFixWidth<1> coefvals(ir.Size(),lh);
               coef.Get()->Evaluate(mir,coefvals);
               FlatMatrix<> shapesy(fely.GetNDof(),ir.Size(),lh);
               dynamic_cast<const BaseScalarFiniteElement & >(fely).CalcShape(ir,shapesy);
               FlatVector<> helper(ir.Size(),lh);
               helper = Trans(shapesy)*coefy;
               for(int ip=0;ip<ir.Size();ip++)
                  val+=helper(ip)*mir[ip].GetWeight()*coefvals(ip,0);
             }
             return val;
           });
   m.def("ProlongateCoefficientFunction", [](PyCF cf_x, int prolongateto) -> PyCF
           {
             auto pcf = make_shared<ProlongateCoefficientFunction>(cf_x.Get(),prolongateto,cf_x.Get()->Dimension(),false);
             pcf->SetDimension(pcf->Dimension());
             return PyCF(pcf);
           });
   m.def("Prolongate", [](PyGF gf_x, PyGF gf_tp )
            {
              static Timer tall("comp.Prolongate"); RegionTimer rall(tall);
              shared_ptr<TPHighOrderFESpace> tpfes = dynamic_pointer_cast<TPHighOrderFESpace>(gf_tp.Get()->GetFESpace());
              LocalHeap lh(100000,"ProlongateFromXSpace");
              if(gf_x.Get()->GetFESpace() == tpfes->Space(-1) )
                tpfes->ProlongateFromXSpace(gf_x.Get(),gf_tp.Get(),lh);
              else
                cout << "GridFunction gf_x is not defined on first space"<<endl;
              });
   m.def("Transfer2StdMesh", [](const PyGF gfutp, PyGF gfustd )
            {
              static Timer tall("comp.Transfer2StdMesh"); RegionTimer rall(tall);
              Transfer2StdMesh(gfutp.Get().get(),gfustd.Get().get());
              return;
             });
  
  
  
  typedef PyWrapper<BaseVTKOutput> PyVTK;
  py::class_<PyVTK>(m, "VTKOutput")
    .def("__init__",
         [](PyVTK *instance, shared_ptr<MeshAccess> ma, py::list coefs_list,
                              py::list names_list, string filename, int subdivision, int only_element)
                           {
                             Array<shared_ptr<CoefficientFunction> > coefs
                               = makeCArrayUnpackWrapper<PyCF> (coefs_list);
                             Array<string > names
                               = makeCArray<string> (names_list);
                             shared_ptr<BaseVTKOutput> ret;
                             if (ma->GetDimension() == 2)
                               ret = make_shared<VTKOutput<2>> (ma, coefs, names, filename, subdivision, only_element);
                             else
                               ret = make_shared<VTKOutput<3>> (ma, coefs, names, filename, subdivision, only_element);
                             new (instance) PyVTK(ret);
                           },

            py::arg("ma"),
            py::arg("coefs")= py::list(),
            py::arg("names") = py::list(),
            py::arg("filename") = "vtkout",
            py::arg("subdivision") = 0,
            py::arg("only_element") = -1
      )

    .def("Do", FunctionPointer([](PyVTK & self, int heapsize)
                               { 
                                 LocalHeap lh (heapsize, "VTKOutput-heap");
                                 self->Do(lh);
                               }),
         py::arg("heapsize")=1000000)
    .def("Do", FunctionPointer([](PyVTK & self, const BitArray * drawelems, int heapsize)
                               { 
                                 LocalHeap lh (heapsize, "VTKOutput-heap");
                                 self->Do(lh, drawelems);
                               }),
         py::arg("drawelems"),py::arg("heapsize")=1000000)
    
    ;

  /////////////////////////////////////////////////////////////////////////////////////

#ifdef PARALLEL
  import_mpi4py();
#endif
}

PYBIND11_PLUGIN(libngcomp) {
  py::module m("comp", "pybind comp");
  ExportNgcomp(m);
  return m.ptr();
}



#endif // NGS_PYTHON
