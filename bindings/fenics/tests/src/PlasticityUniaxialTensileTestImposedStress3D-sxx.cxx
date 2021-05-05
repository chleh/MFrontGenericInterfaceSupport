/*!
 * \file
 * bindings/fenics/tests/PlasticityUniaxialTensileTestImposedStress3D-sxx.cxx
 * \brief  This program tests the elastic and plastic load-displacement
 * responses for a unit cube in uniaxial tension with an Von Mises
 * (J2) plastic behaviour with linear strain hardening.
 * \author Thomas Helfer
 * \date   14/12/2018
 * \copyright (C) Copyright Thomas Helfer 2018.
 * Use, modification and distribution are subject
 * to one of the following licences:
 * - GNU Lesser General Public License (LGPL), Version 3.0. (See accompanying
 *   file LGPL-3.0.txt)
 * - CECILL-C,  Version 1.0 (See accompanying files
 *   CeCILL-C_V1-en.txt and CeCILL-C_V1-fr.txt).
 *
 * \note This file contains material strongly inspired by the
 * fenics-solid-materials by Kristian Oelgaard and Garth N. Wells.
 * See <https://bitbucket.org/fenics-apps/fenics-solid-mechanics> for
 * details.
 */

#include <memory>
#include <cstdlib>
#include <dolfin/common/Array.h>
#include <dolfin/fem/assemble.h>
#include <dolfin/fem/DirichletBC.h>
#include <dolfin/fem/FiniteElement.h>
#include <dolfin/fem/SystemAssembler.h>
#include <dolfin/function/Function.h>
#include <dolfin/log/Logger.h>
#include <dolfin/nls/NewtonSolver.h>
#include <dolfin/function/Constant.h>
#include <dolfin/function/Expression.h>
#include <dolfin/mesh/Facet.h>
#include <dolfin/generation/UnitCubeMesh.h>
#include <dolfin/mesh/MeshFunction.h>
#include <dolfin/mesh/SubDomain.h>

#include "MGISSmallStrainFormulation3D.h"
#include "MGIS/Behaviour/Behaviour.hxx"
#include "MGIS/FEniCS/NonLinearMaterial.hxx"
#include "MGIS/FEniCS/NonLinearMechanicalProblem.hxx"
#include "MGIS/FEniCS/FEniCSTestingUtilities.hxx"

// force at the right end
struct Load : public dolfin::Expression {
  Load(const double& t_) : dolfin::Expression(3), t(t_) {}
  void eval(Eigen::Ref<Eigen::VectorXd> values,
            Eigen::Ref<const Eigen::VectorXd>) const override {
    values[0] = 1.0e5 * t;
    values[1] = 0.0;
    values[2] = 0.0;
  }
  ~Load() override = default;

 private:
  const double& t;
};

int main() {
  // getting the path to the test library
  auto library = std::getenv("MGIS_TEST_BEHAVIOURS_LIBRARY");
  if (library == nullptr) {
    std::exit(EXIT_FAILURE);
  }
  // create mesh
  auto mesh = std::make_shared<dolfin::UnitCubeMesh>(4, 4, 4);
  auto boundaries = mgis::fenics::getUnitCubeBoundaries();
  // auto mesh = std::make_shared<dolfin::UnitCubeMesh>(1, 1, 1);
  // Time parameter
  double t = 0.0;
  // Source term, RHS
  auto f = std::make_shared<dolfin::Constant>(0.0, 0.0, 0.0);
  // function space
  auto V = std::make_shared<MGISSmallStrainFormulation3D::FunctionSpace>(mesh);

  // Extract elements for stress and tangent
  std::shared_ptr<const dolfin::FiniteElement> element_t;
  std::shared_ptr<const dolfin::FiniteElement> element_s;
  {
    MGISSmallStrainFormulation3D::BilinearForm::CoefficientSpace_t Vt(mesh);
    element_t = Vt.element();
  }

  MGISSmallStrainFormulation3D::LinearForm::CoefficientSpace_s Vs(mesh);
  element_s = Vs.element();

  // Create boundary conditions (use SubSpace to apply simply
  // supported BCs)
  auto zero = std::make_shared<dolfin::Constant>(0.0);
  auto boundary_load = std::make_shared<Load>(t);

  std::vector<std::shared_ptr<const dolfin::DirichletBC>> bcs;
  bcs.push_back(std::make_shared<dolfin::DirichletBC>(V->sub(0), zero,
                                                      boundaries["sx1"]));
  bcs.push_back(std::make_shared<dolfin::DirichletBC>(V->sub(1), zero,
                                                      boundaries["sy1"]));
  bcs.push_back(std::make_shared<dolfin::DirichletBC>(V->sub(2), zero,
                                                      boundaries["sz1"]));

  // Mark loading boundary
  auto load_marker = std::make_shared<dolfin::MeshFunction<std::size_t>>(
      mesh, mesh->topology().dim() - 1, 0);
  boundaries["sx2"]->mark(*load_marker, 1);

  // Solution function
  auto u = std::make_shared<dolfin::Function>(V);

  auto b = mgis::behaviour::load(library, "Plasticity",
                                 mgis::behaviour::Hypothesis::TRIDIMENSIONAL);
  mgis::fenics::NonLinearMaterial m(u, element_t, element_s, b);
  setExternalStateVariable(m.s0, "Temperature", 293.15);
  setExternalStateVariable(m.s1, "Temperature", 293.15);

  // // Create forms and attach functions
  auto a = std::make_shared<MGISSmallStrainFormulation3D::BilinearForm>(V, V);
  a->t = m.getTangentOperatorFunction();
  a->ds = load_marker;
  auto L = std::make_shared<MGISSmallStrainFormulation3D::LinearForm>(V);
  L->f = f;
  L->h = boundary_load;
  L->s = m.getThermodynamicForcesFunction();

  // create non linear material problem
  mgis::fenics::NonLinearMechanicalProblem nonlinear_problem(a, L, u, bcs);

  // // Displacement and load integration functionals
  auto M_d = std::make_shared<MGISSmallStrainFormulation3D::Form_M_d>(mesh, u);
  auto M_f = std::make_shared<MGISSmallStrainFormulation3D::Form_M_f>(
      mesh, boundary_load);
  M_d->ds = load_marker;
  M_f->ds = load_marker;

  // Create nonlinear solver and set parameters
  dolfin::NewtonSolver nonlinear_solver;
  nonlinear_solver.parameters["convergence_criterion"] = "incremental";
  nonlinear_solver.parameters["maximum_iterations"] = 50;
  nonlinear_solver.parameters["relative_tolerance"] = 1.0e-6;
  nonlinear_solver.parameters["absolute_tolerance"] = 1.0e-15;

  // Structures to hold load-disp data
  std::vector<double> disp, load, p;
  disp.push_back(0.0);
  load.push_back(0.0);
  p.push_back(0.0);

  // Solver loop
  mgis::size_type step = 0;
  mgis::size_type steps = 10;
  double dt = 0.001;
  while (step < steps) {
    m.setTimeIncrement(dt);
    t += dt;
    ++step;
    std::cout << "step begin: " << step << std::endl;
    std::cout << "time: " << t << std::endl;
    // solve the non-linear problem
    nonlinear_solver.solve(nonlinear_problem, *u->vector());
    // update state variables
    mgis::behaviour::update(m);
    // post-processings
    const double u_avg = assemble(*M_d);
    disp.push_back(u_avg);
    const double force = assemble(*M_f);
    load.push_back(force);
    p.push_back(m.s0.internal_state_variables[6]);
  }

  for (mgis::size_type i = 0; i != disp.size(); ++i) {
    std::cout << disp[i] << " " << load[i] << " " << p[i] << '\n';
  }

  return EXIT_SUCCESS;
}
