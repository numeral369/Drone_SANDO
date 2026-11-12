// grb_compat implementation. See include/gurobi_c++.h for scope and disclaimer.
#include "gurobi_c++.h"

#include <Eigen/Dense>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>

#include "gi_qp.hpp"

// ----------------------------------------------------------------------------------------------
// Expressions
// ----------------------------------------------------------------------------------------------
double GRBVar::get(GRB_DoubleAttr a) const {
  if (a != GRB_DoubleAttr_X) throw GRBException("grb_compat: unsupported var attribute");
  if (!d_) throw GRBException("grb_compat: empty GRBVar");
  return d_->x;
}

void GRBLinExpr::compress() {
  if (t_.size() < 2) return;
  std::sort(t_.begin(), t_.end(), [](const Term& a, const Term& b) { return a.v.get() < b.v.get(); });
  std::vector<Term> out;
  out.reserve(t_.size());
  for (auto& t : t_) {
    if (!out.empty() && out.back().v == t.v) out.back().c += t.c;
    else out.push_back(t);
  }
  out.erase(std::remove_if(out.begin(), out.end(), [](const Term& t) { return t.c == 0.0; }), out.end());
  t_.swap(out);
}

GRBLinExpr& GRBLinExpr::operator+=(const GRBLinExpr& o) {
  k_ += o.k_;
  t_.insert(t_.end(), o.t_.begin(), o.t_.end());
  if (t_.size() > 48) compress();
  return *this;
}
GRBLinExpr& GRBLinExpr::operator-=(const GRBLinExpr& o) {
  k_ -= o.k_;
  t_.reserve(t_.size() + o.t_.size());
  for (const auto& t : o.t_) t_.push_back({t.v, -t.c});
  if (t_.size() > 48) compress();
  return *this;
}
GRBLinExpr& GRBLinExpr::operator*=(double s) {
  k_ *= s;
  for (auto& t : t_) t.c *= s;
  return *this;
}
double GRBLinExpr::getValue() const {
  double v = k_;
  for (const auto& t : t_) v += t.c * t.v->x;
  return v;
}

GRBLinExpr operator+(const GRBLinExpr& a, const GRBLinExpr& b) { GRBLinExpr r(a); r += b; return r; }
GRBLinExpr operator-(const GRBLinExpr& a, const GRBLinExpr& b) { GRBLinExpr r(a); r -= b; return r; }
GRBLinExpr operator*(double s, const GRBLinExpr& a) { GRBLinExpr r(a); r *= s; return r; }
GRBLinExpr operator*(const GRBLinExpr& a, double s) { GRBLinExpr r(a); r *= s; return r; }
GRBLinExpr operator/(const GRBLinExpr& a, double s) { GRBLinExpr r(a); r *= 1.0 / s; return r; }

GRBQuadExpr operator*(const GRBLinExpr& a0, const GRBLinExpr& b0) {
  GRBLinExpr a(a0), b(b0);
  a.compress();
  b.compress();
  GRBQuadExpr q(a.getConstant() * b + b.getConstant() * GRBLinExpr(a) - a.getConstant() * b.getConstant());
  // (sum ai xi + a0)(sum bj xj + b0) = sum ai bj xi xj + a0*B + b0*A + a0 b0,  where A,B include constants.
  for (const auto& ta : a.terms())
    for (const auto& tb : b.terms()) q.addQTerm(ta.v, tb.v, ta.c * tb.c);
  return q;
}
GRBQuadExpr& GRBQuadExpr::operator+=(const GRBQuadExpr& o) {
  lin_ += o.lin_;
  q_.insert(q_.end(), o.q_.begin(), o.q_.end());
  return *this;
}
GRBQuadExpr& GRBQuadExpr::operator-=(const GRBQuadExpr& o) {
  lin_ -= o.lin_;
  for (const auto& t : o.q_) q_.push_back({t.a, t.b, -t.c});
  return *this;
}
GRBQuadExpr& GRBQuadExpr::operator*=(double s) {
  lin_ *= s;
  for (auto& t : q_) t.c *= s;
  return *this;
}
double GRBQuadExpr::getValue() const {
  double v = lin_.getValue();
  for (const auto& t : q_) v += t.c * t.a->x * t.b->x;
  return v;
}
GRBQuadExpr operator+(const GRBQuadExpr& a, const GRBQuadExpr& b) { GRBQuadExpr r(a); r += b; return r; }
GRBQuadExpr operator-(const GRBQuadExpr& a, const GRBQuadExpr& b) { GRBQuadExpr r(a); r -= b; return r; }
GRBQuadExpr operator*(double s, const GRBQuadExpr& a) { GRBQuadExpr r(a); r *= s; return r; }
GRBQuadExpr operator*(const GRBQuadExpr& a, double s) { GRBQuadExpr r(a); r *= s; return r; }

static GRBTempConstr mk(const GRBLinExpr& e, char sense) {
  GRBTempConstr t;
  t.expr = GRBQuadExpr(e);
  t.sense = sense;
  return t;
}
GRBTempConstr operator<=(const GRBLinExpr& a, const GRBLinExpr& b) { return mk(a - b, GRB_LESS_EQUAL); }
GRBTempConstr operator>=(const GRBLinExpr& a, const GRBLinExpr& b) { return mk(a - b, GRB_GREATER_EQUAL); }
GRBTempConstr operator==(const GRBLinExpr& a, const GRBLinExpr& b) { return mk(a - b, GRB_EQUAL); }
GRBTempConstr operator<=(const GRBLinExpr& a, double b) { return mk(a - GRBLinExpr(b), GRB_LESS_EQUAL); }
GRBTempConstr operator>=(const GRBLinExpr& a, double b) { return mk(a - GRBLinExpr(b), GRB_GREATER_EQUAL); }
GRBTempConstr operator==(const GRBLinExpr& a, double b) { return mk(a - GRBLinExpr(b), GRB_EQUAL); }
GRBTempConstr operator<=(const GRBQuadExpr& a, double b) {
  GRBTempConstr t;
  t.expr = a - GRBQuadExpr(b);
  t.sense = GRB_LESS_EQUAL;
  t.quadratic = true;
  return t;
}
GRBTempConstr operator>=(const GRBQuadExpr& a, double b) {
  GRBTempConstr t = (a <= b);
  t.sense = GRB_GREATER_EQUAL;
  return t;
}

// ----------------------------------------------------------------------------------------------
// Model
// ----------------------------------------------------------------------------------------------
namespace grbc {

struct ModelImpl {
  std::vector<std::shared_ptr<VarData>> vars;     // committed
  std::vector<std::shared_ptr<VarData>> pending;  // added since last update
  std::vector<GRBVar> var_handles;                // backing store for getVars()
  std::vector<std::shared_ptr<ConstrData>> cons;  // linear + indicator + quadratic
  GRBQuadExpr obj;
  int obj_sense = GRB_MINIMIZE;
  GRBCallback* cb = nullptr;
  double time_limit = std::numeric_limits<double>::infinity();
  int status = GRB_LOADED;
  double runtime = 0.0;
  double objval = std::numeric_limits<double>::quiet_NaN();
  long n_qps = 0, n_leaves = 0;

  void update() {
    vars.erase(std::remove_if(vars.begin(), vars.end(), [](const std::shared_ptr<VarData>& v) { return v->removed; }),
               vars.end());
    for (auto& v : pending)
      if (!v->removed) vars.push_back(v);
    pending.clear();
    cons.erase(std::remove_if(cons.begin(), cons.end(), [](const std::shared_ptr<ConstrData>& c) { return c->removed; }),
               cons.end());
    for (size_t i = 0; i < vars.size(); ++i) vars[i]->id = static_cast<long>(i);
  }

  bool interrupted() {
    if (!cb) return false;
    cb->callback();
    return cb->aborted_.load();
  }

  void optimize();
};

namespace {
struct Row {  // a' x  (sense)  b   over continuous index space
  Eigen::VectorXd a;
  double b;
  char sense;
};
}  // namespace

void ModelImpl::optimize() {
  using clk = std::chrono::steady_clock;
  const auto t_start = clk::now();
  auto elapsed = [&]() { return std::chrono::duration<double>(clk::now() - t_start).count(); };
  status = GRB_LOADED;
  objval = std::numeric_limits<double>::quiet_NaN();
  n_qps = 0;
  n_leaves = 0;
  if (cb) cb->aborted_ = false;
  update();

  // --- index continuous and binary variables ---
  std::vector<VarData*> cont, bins;
  for (auto& v : vars) {
    if (v->type == GRB_CONTINUOUS) { v->id = static_cast<long>(cont.size()); cont.push_back(v.get()); }
    else if (v->type == GRB_BINARY) { v->id = static_cast<long>(bins.size()); bins.push_back(v.get()); }
    else throw GRBException("grb_compat: only continuous and binary variables are supported");
  }
  const int n = static_cast<int>(cont.size());
  const int nb = static_cast<int>(bins.size());

  auto split = [&](const GRBLinExpr& e, Eigen::VectorXd& a, std::vector<std::pair<int, double>>& bterms) {
    a = Eigen::VectorXd::Zero(n);
    bterms.clear();
    for (const auto& t : e.terms()) {
      if (t.v->removed) continue;  // removed vars drop out (Gurobi semantics)
      if (t.v->type == GRB_CONTINUOUS) a(t.v->id) += t.c;
      else bterms.push_back({static_cast<int>(t.v->id), t.c});
    }
  };

  // --- classify constraints ---
  std::vector<Row> base;                     // always-on continuous rows
  std::vector<std::vector<Row>> ind_rows(nb);  // rows switched on when binary == 1
  struct BinCon { std::vector<std::pair<int, double>> t; double rhs; char sense; };
  std::vector<BinCon> bincons;
  for (auto& c : cons) {
    if (c->quadratic) throw GRBException("grb_compat: quadratic constraints are not supported (use L-inf dynamic limits)");
    Eigen::VectorXd a;
    std::vector<std::pair<int, double>> bt;
    split(c->expr, a, bt);
    const double rhs = -c->expr.getConstant();
    if (c->indicator) {
      if (!bt.empty()) throw GRBException("grb_compat: binary inside indicator body");
      if (c->binvar->removed) continue;
      if (c->binval != 1) throw GRBException("grb_compat: only indicator value 1 supported");
      ind_rows[c->binvar->id].push_back({a, rhs, c->sense});
    } else if (bt.empty()) {
      base.push_back({a, rhs, c->sense});
    } else if (a.isZero(0.0)) {
      bincons.push_back({bt, rhs, c->sense});
    } else {
      throw GRBException("grb_compat: mixed binary/continuous linear constraint not supported");
    }
  }
  for (VarData* v : cont) {  // variable bounds
    if (v->lb > -1e90) { Row r{Eigen::VectorXd::Zero(n), v->lb, GRB_GREATER_EQUAL}; r.a(v->id) = 1; base.push_back(r); }
    if (v->ub < 1e90) { Row r{Eigen::VectorXd::Zero(n), v->ub, GRB_LESS_EQUAL}; r.a(v->id) = 1; base.push_back(r); }
  }

  // --- objective: 0.5 x'Hx + c'x + k ---
  Eigen::MatrixXd H = Eigen::MatrixXd::Zero(n, n);
  Eigen::VectorXd cvec;
  {
    std::vector<std::pair<int, double>> bt;
    split(obj.linear(), cvec, bt);
    for (auto& p : bt) if (p.second != 0.0) throw GRBException("grb_compat: binaries in objective not supported");
    for (const auto& q : obj.qterms()) {
      if (q.a->removed || q.b->removed || q.c == 0.0) continue;
      if (q.a->type != GRB_CONTINUOUS || q.b->type != GRB_CONTINUOUS)
        throw GRBException("grb_compat: binaries in objective not supported");
      const int i = static_cast<int>(q.a->id), j = static_cast<int>(q.b->id);
      H(i, j) += q.c;
      H(j, i) += q.c;
    }
    if (obj_sense == GRB_MAXIMIZE) { H = -H; cvec = -cvec; }
  }
  // --- eliminate always-on equality rows exactly: x = xp + Z y (Z = null-space basis) ---
  // Equalities only appear in the base rows for SANDO (continuity / boundary conditions when variable
  // elimination is off). The reduced Hessian Z'HZ is then positive definite in practice; a tiny relative
  // regularisation is added only if it is not (Goldfarb-Idnani needs strict convexity).
  Eigen::MatrixXd Z;
  Eigen::VectorXd xp;
  {
    std::vector<const Row*> eqs;
    std::vector<Row> ineq_base;
    for (auto& r : base) { if (r.sense == GRB_EQUAL) eqs.push_back(&r); else ineq_base.push_back(r); }
    if (eqs.empty()) {
      Z = Eigen::MatrixXd::Identity(n, n);
      xp = Eigen::VectorXd::Zero(n);
    } else {
      Eigen::MatrixXd Aeq(eqs.size(), n);
      Eigen::VectorXd beq(eqs.size());
      for (size_t i = 0; i < eqs.size(); ++i) { Aeq.row(i) = eqs[i]->a.transpose(); beq(i) = eqs[i]->b; }
      Eigen::CompleteOrthogonalDecomposition<Eigen::MatrixXd> cod(Aeq);
      cod.setThreshold(1e-12);
      xp = cod.solve(beq);
      if ((Aeq * xp - beq).norm() > 1e-8 * (1.0 + beq.norm())) { status = GRB_INFEASIBLE; runtime = elapsed(); return; }
      const int rank = static_cast<int>(cod.rank());
      Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(Aeq.transpose());
      Eigen::MatrixXd Q = qr.householderQ();
      Z = Q.rightCols(n - rank);
    }
    base.swap(ineq_base);
  }
  for (auto& rows : ind_rows)
    for (auto& r : rows)
      if (r.sense == GRB_EQUAL) throw GRBException("grb_compat: equality inside indicator not supported");
  const int ny = static_cast<int>(Z.cols());
  Eigen::MatrixXd Hy = Z.transpose() * H * Z;
  Eigen::VectorXd gy = Z.transpose() * (H * xp + cvec);
  if (ny > 0) {
    Eigen::LLT<Eigen::MatrixXd> llt(Hy);
    if (llt.info() != Eigen::Success) {
      const double scale = std::max(1.0, Hy.diagonal().cwiseAbs().maxCoeff());
      Hy.diagonal().array() += 1e-10 * scale;
    }
  }

  // --- binary structure: groups "sum b >= 1" (monotone indicators -> exactly one per group) ---
  std::vector<int> fixed0(nb, 0);
  for (int i = 0; i < nb; ++i)
    if (bins[i]->ub < 0.5) fixed0[i] = 1;
  std::vector<int> fixed1(nb, 0);
  std::vector<std::vector<int>> groups;
  bool structured = true;
  for (auto& bc : bincons) {
    bool all_one = true;
    for (auto& t : bc.t) all_one = all_one && (t.second == 1.0);
    const bool single = bc.t.size() == 1 && bc.t[0].second == 1.0;
    if (single && ((bc.sense == GRB_EQUAL && bc.rhs == 0.0) || (bc.sense == GRB_LESS_EQUAL && bc.rhs <= 0.0))) {
      fixed0[bc.t[0].first] = 1;
    } else if (single && ((bc.sense == GRB_EQUAL && bc.rhs == 1.0) || (bc.sense == GRB_GREATER_EQUAL && bc.rhs == 1.0))) {
      fixed1[bc.t[0].first] = 1;
    } else if (all_one && bc.sense == GRB_GREATER_EQUAL && bc.rhs == 1.0) {
      std::vector<int> g;
      for (auto& t : bc.t) g.push_back(t.first);
      groups.push_back(g);
    } else {
      structured = false;
    }
  }
  if (!structured) throw GRBException("grb_compat: unsupported binary constraint structure");
  // Binaries forced to 1: their indicator rows are always on; groups they belong to are satisfied.
  for (int i = 0; i < nb; ++i) {
    if (fixed1[i] && fixed0[i]) { status = GRB_INFEASIBLE; runtime = elapsed(); return; }
    if (fixed1[i])
      for (auto& r : ind_rows[i]) base.push_back(r);
  }
  {
    std::vector<std::vector<int>> open;
    std::vector<int> used(nb, 0);
    for (auto& g : groups) {
      bool sat = false;
      for (int b : g) sat = sat || fixed1[b];
      if (sat) continue;
      // Overlapping groups would break the "exactly one per group" enumeration -> unsupported.
      for (int b : g) { if (used[b]) structured = false; used[b] = 1; }
      open.push_back(g);
    }
    if (!structured) throw GRBException("grb_compat: overlapping binary groups not supported");
    groups.swap(open);
  }

  // --- QP helper ---
  auto solve_qp = [&](const std::vector<const std::vector<Row>*>& extra, Eigen::VectorXd& x, double& f) -> bool {
    ++n_qps;
    int m = static_cast<int>(base.size());
    for (auto* e : extra) m += static_cast<int>(e->size());
    // rows in x-space:  a'x (<= or >=) b   ->  in y-space with x = xp + Z y:  (Z'a)'y (sense) b - a'xp
    Eigen::MatrixXd CI(ny, m);
    Eigen::VectorXd ci0(m);
    int ii = 0;
    auto put = [&](const std::vector<Row>& rows) {
      for (auto& r : rows) {
        Eigen::VectorXd ay = Z.transpose() * r.a;
        double by = r.b - r.a.dot(xp);
        const double nrm = ay.norm();
        if (nrm < 1e-12) {  // constant row: check directly, then drop
          const bool ok = (r.sense == GRB_LESS_EQUAL) ? (by >= -1e-9) : (by <= 1e-9);
          if (!ok) { ii = -1; return; }
          continue;
        }
        if (r.sense == GRB_LESS_EQUAL) { CI.col(ii) = -ay / nrm; ci0(ii) = by / nrm; }  // by - ay'y >= 0
        else { CI.col(ii) = ay / nrm; ci0(ii) = -by / nrm; }                          // ay'y - by >= 0
        ++ii;
      }
    };
    put(base);
    for (auto* e : extra) { if (ii < 0) break; put(*e); }
    if (ii < 0) return false;
    giqp::Result r = giqp::solve(Hy, gy, Eigen::MatrixXd(ny, 0), Eigen::VectorXd(0), CI.leftCols(ii), ci0.head(ii));
    if (r.status != giqp::Status::Optimal) return false;
    x = xp + Z * r.x;
    f = 0.5 * x.dot(H * x) + cvec.dot(x);
    return true;
  };

  Eigen::VectorXd best_x;
  double best_f = std::numeric_limits<double>::infinity();
  std::vector<int> best_assign;  // option index per group
  bool aborted = false, timed_out = false;
  const double tol = 1e-9;

  // Depth-first branch and bound over groups. A node fixes the first k groups; its QP (without the
  // remaining groups' corridor rows) is a valid lower bound because indicators only ADD constraints.
  std::vector<int> assign(groups.size(), -1);
  std::function<void(size_t, double)> dfs = [&](size_t k, double /*parent_f*/) {
    if (aborted || timed_out) return;
    if (k == groups.size()) return;
    struct Child { int opt; double f; Eigen::VectorXd x; };
    std::vector<Child> kids;
    for (size_t o = 0; o < groups[k].size(); ++o) {
      const int b = groups[k][o];
      if (fixed0[b]) continue;
      if (interrupted()) { aborted = true; return; }
      if (elapsed() > time_limit) { timed_out = true; return; }
      std::vector<const std::vector<Row>*> extra;
      for (size_t g = 0; g < k; ++g) extra.push_back(&ind_rows[groups[g][assign[g]]]);
      extra.push_back(&ind_rows[b]);
      Eigen::VectorXd x;
      double f;
      if (!solve_qp(extra, x, f)) continue;
      if (f >= best_f - tol * std::max(1.0, std::fabs(best_f))) continue;
      kids.push_back({static_cast<int>(o), f, x});
    }
    std::sort(kids.begin(), kids.end(), [](const Child& a, const Child& b) { return a.f < b.f; });
    for (auto& c : kids) {
      if (c.f >= best_f - tol * std::max(1.0, std::fabs(best_f))) continue;
      assign[k] = c.opt;
      if (k + 1 == groups.size()) {
        ++n_leaves;
        best_f = c.f;
        best_x = c.x;
        best_assign = assign;
      } else {
        dfs(k + 1, c.f);
      }
      if (aborted || timed_out) return;
    }
  };

  if (groups.empty()) {
    // pure QP (binaries, if any, are free and only relax -> set to 0)
    Eigen::VectorXd x;
    double f;
    if (solve_qp({}, x, f)) { best_x = x; best_f = f; ++n_leaves; }
  } else {
    dfs(0, -std::numeric_limits<double>::infinity());
  }

  runtime = elapsed();
  if (aborted) { status = GRB_INTERRUPTED; return; }
  if (timed_out && !std::isfinite(best_f)) { status = GRB_TIME_LIMIT; return; }
  if (timed_out) { status = GRB_TIME_LIMIT; }  // incumbent exists but optimality not proven
  if (!std::isfinite(best_f)) { status = GRB_INFEASIBLE; return; }

  for (int i = 0; i < n; ++i) cont[i]->x = best_x(i);
  for (int i = 0; i < nb; ++i) bins[i]->x = fixed1[i] ? 1.0 : 0.0;
  for (size_t g = 0; g < groups.size(); ++g) bins[groups[g][best_assign[g]]]->x = 1.0;
  objval = obj.getValue();
  if (!timed_out) status = GRB_OPTIMAL;
}

}  // namespace grbc

GRBModel::GRBModel(const GRBEnv&) : impl_(new grbc::ModelImpl) {}
GRBModel::GRBModel(GRBModel&&) noexcept = default;
GRBModel& GRBModel::operator=(GRBModel&&) noexcept = default;
GRBModel::~GRBModel() = default;

GRBVar GRBModel::addVar(double lb, double ub, double obj, char type, const std::string& name) {
  auto d = std::make_shared<grbc::VarData>();
  d->lb = lb;
  d->ub = ub;
  d->type = type;
  d->name = name;
  if (type == GRB_BINARY) { d->lb = std::max(0.0, lb); d->ub = std::min(1.0, ub); }
  impl_->pending.push_back(d);
  GRBVar v(d);
  if (obj != 0.0) impl_->obj += GRBQuadExpr(GRBLinExpr(v, obj));
  return v;
}

GRBConstr GRBModel::addConstr(const GRBTempConstr& tc, const std::string& name) {
  auto d = std::make_shared<grbc::ConstrData>();
  if (tc.quadratic) throw GRBException("grb_compat: use addQConstr for quadratic constraints");
  d->expr = tc.expr.linear();
  d->expr.compress();
  d->sense = tc.sense;
  d->name = name;
  impl_->cons.push_back(d);
  return GRBConstr(d);
}
GRBConstr GRBModel::addConstr(const GRBLinExpr& lhs, char sense, double rhs, const std::string& name) {
  GRBTempConstr t;
  t.expr = GRBQuadExpr(lhs - GRBLinExpr(rhs));
  t.sense = sense;
  return addConstr(t, name);
}
GRBGenConstr GRBModel::addGenConstrIndicator(const GRBVar& binvar, int binval, const GRBLinExpr& expr, char sense,
                                             double rhs, const std::string& name) {
  auto d = std::make_shared<grbc::ConstrData>();
  d->expr = expr - GRBLinExpr(rhs);
  d->expr.compress();
  d->sense = sense;
  d->name = name;
  d->indicator = true;
  d->binvar = binvar.data();
  d->binval = binval;
  impl_->cons.push_back(d);
  return GRBGenConstr(d);
}
GRBGenConstr GRBModel::addGenConstrIndicator(const GRBVar& binvar, int binval, const GRBTempConstr& tc,
                                             const std::string& name) {
  return addGenConstrIndicator(binvar, binval, tc.expr.linear(), tc.sense, 0.0, name);
}
GRBQConstr GRBModel::addQConstr(const GRBTempConstr& tc, const std::string& name) {
  auto d = std::make_shared<grbc::ConstrData>();
  d->expr = tc.expr.linear();
  d->sense = tc.sense;
  d->name = name;
  d->quadratic = true;
  impl_->cons.push_back(d);
  return GRBQConstr(d);
}
void GRBModel::remove(const GRBVar& v) { if (v.data()) v.data()->removed = true; }
void GRBModel::remove(const GRBConstr& c) { if (c.data()) c.data()->removed = true; }
void GRBModel::setObjective(const GRBQuadExpr& obj, int sense) { impl_->obj = obj; impl_->obj_sense = sense; }
void GRBModel::setObjective(const GRBLinExpr& obj, int sense) { setObjective(GRBQuadExpr(obj), sense); }
void GRBModel::setCallback(GRBCallback* cb) { impl_->cb = cb; }
void GRBModel::update() { impl_->update(); }
void GRBModel::optimize() { impl_->optimize(); }
GRBVar* GRBModel::getVars() {
  impl_->var_handles.clear();
  for (auto& v : impl_->vars) impl_->var_handles.emplace_back(v);
  return impl_->var_handles.data();
}
void GRBModel::set(GRB_IntParam, int) {}
void GRBModel::set(GRB_DoubleParam p, double v) {
  if (p == GRB_DoubleParam_TimeLimit) impl_->time_limit = v;
}
void GRBModel::set(GRB_StringAttr, const std::string&) {}
int GRBModel::get(GRB_IntAttr a) const {
  switch (a) {
    case GRB_IntAttr_Status: return impl_->status;
    case GRB_IntAttr_NumVars: return static_cast<int>(impl_->vars.size());
  }
  throw GRBException("grb_compat: unsupported int attribute");
}
double GRBModel::get(GRB_DoubleAttr a) const {
  switch (a) {
    case GRB_DoubleAttr_Runtime: return impl_->runtime;
    case GRB_DoubleAttr_ObjVal:
      if (impl_->status != GRB_OPTIMAL && impl_->status != GRB_TIME_LIMIT)
        throw GRBException("grb_compat: no solution available", 10005);
      return impl_->objval;
    default: break;
  }
  throw GRBException("grb_compat: unsupported model attribute");
}
long GRBModel::lastNumQPs() const { return impl_->n_qps; }
long GRBModel::lastNumLeaves() const { return impl_->n_leaves; }
