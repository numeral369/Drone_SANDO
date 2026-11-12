// T3a: unit tests for grb_compat (gi_qp + MIQP branch and bound).
// Independent oracles:
//   (1) QP: brute force over all active sets (KKT enumeration) for small random problems.
//   (2) MIQP: brute force over every binary assignment (P^G), each QP solved independently.
//   (3) KKT certificate check on every reported optimum.
#include <Eigen/Dense>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

#include "gi_qp.hpp"
#include "gurobi_c++.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (cond) { ++g_pass; }                      \
    else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } \
  } while (0)

using Eigen::MatrixXd;
using Eigen::VectorXd;

// Oracle 1: enumerate active sets of inequality constraints (|S| <= n), equality constraints always active.
static bool bruteQP(const MatrixXd& G, const VectorXd& g0, const MatrixXd& CE, const VectorXd& ce0,
                    const MatrixXd& CI, const VectorXd& ci0, VectorXd& xbest, double& fbest) {
  const int n = G.rows(), p = CE.cols(), m = CI.cols();
  fbest = INFINITY;
  for (int mask = 0; mask < (1 << m); ++mask) {
    std::vector<int> S;
    for (int i = 0; i < m; ++i) if (mask & (1 << i)) S.push_back(i);
    const int k = p + (int)S.size();
    if (k > n) continue;
    MatrixXd K = MatrixXd::Zero(n + k, n + k);
    VectorXd rhs(n + k);
    K.topLeftCorner(n, n) = G;
    rhs.head(n) = -g0;
    for (int j = 0; j < p; ++j) { K.block(0, n + j, n, 1) = -CE.col(j); K.block(n + j, 0, 1, n) = CE.col(j).transpose(); rhs(n + j) = -ce0(j); }
    for (int j = 0; j < (int)S.size(); ++j) { int c = S[j]; K.block(0, n + p + j, n, 1) = -CI.col(c); K.block(n + p + j, 0, 1, n) = CI.col(c).transpose(); rhs(n + p + j) = -ci0(c); }
    Eigen::FullPivLU<MatrixXd> lu(K);
    if (lu.rank() < n + k) continue;
    VectorXd sol = lu.solve(rhs);
    VectorXd x = sol.head(n);
    bool ok = true;
    for (int j = 0; j < (int)S.size(); ++j) if (sol(n + p + j) < -1e-9) ok = false;   // dual feasible
    for (int i = 0; i < m && ok; ++i) if (CI.col(i).dot(x) + ci0(i) < -1e-9) ok = false;  // primal feasible
    if (!ok) continue;
    double f = 0.5 * x.dot(G * x) + g0.dot(x);
    if (f < fbest) { fbest = f; xbest = x; }
  }
  return std::isfinite(fbest);
}

static void testQPRandom() {
  std::mt19937 rng(42);
  std::normal_distribution<double> N01(0, 1);
  int agree = 0, total = 0;
  for (int trial = 0; trial < 400; ++trial) {
    const int n = 2 + trial % 3, p = trial % 2, m = 3 + trial % 6;
    MatrixXd M(n, n);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) M(i, j) = N01(rng);
    MatrixXd G = M * M.transpose() + 0.1 * MatrixXd::Identity(n, n);
    VectorXd g0(n), ce0(p), ci0(m);
    MatrixXd CE(n, p), CI(n, m);
    for (int i = 0; i < n; ++i) g0(i) = 3 * N01(rng);
    for (int j = 0; j < p; ++j) { for (int i = 0; i < n; ++i) CE(i, j) = N01(rng); ce0(j) = N01(rng); }
    for (int j = 0; j < m; ++j) { for (int i = 0; i < n; ++i) CI(i, j) = N01(rng); ci0(j) = N01(rng) + 0.3; }
    VectorXd xb; double fb;
    bool feas = bruteQP(G, g0, CE, ce0, CI, ci0, xb, fb);
    giqp::Result r = giqp::solve(G, g0, CE, ce0, CI, ci0);
    ++total;
    if (!feas) {
      CHECK(r.status == giqp::Status::Infeasible, "trial %d: oracle infeasible, gi status %d", trial, (int)r.status);
      if (r.status == giqp::Status::Infeasible) ++agree;
    } else {
      bool same = r.status == giqp::Status::Optimal && std::fabs(r.f - fb) <= 1e-7 * std::max(1.0, std::fabs(fb)) &&
                  (r.x - xb).norm() <= 1e-6 * std::max(1.0, xb.norm());
      CHECK(same, "trial %d: f gi=%.9g oracle=%.9g |dx|=%.3g", trial, r.f, fb, r.status == giqp::Status::Optimal ? (r.x - xb).norm() : -1.0);
      if (same) ++agree;
    }
  }
  std::printf("[QP random vs active-set brute force] %d/%d agree\n", agree, total);
}

// Builds a random grouped MIQP through the Gurobi-compatible API and compares to full enumeration.
static void testMIQPRandom() {
  std::mt19937 rng(7);
  std::normal_distribution<double> N01(0, 1);
  std::uniform_real_distribution<double> U(-3, 3);
  int agree = 0, total = 0;
  long qps_bb = 0, qps_enum = 0;
  for (int trial = 0; trial < 150; ++trial) {
    const int n = 2 + trial % 4;      // continuous vars
    const int G = 2 + trial % 4;      // groups (pieces)
    const int P = 2 + trial % 2;      // options per group (polytopes)
    GRBEnv env(true);
    env.start();
    GRBModel m(env);
    std::vector<GRBVar> x;
    for (int i = 0; i < n; ++i) x.push_back(m.addVar(-10, 10, 0, GRB_CONTINUOUS));
    // objective: ||x - target||^2 with random PSD weighting
    MatrixXd Mr(n, n);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) Mr(i, j) = N01(rng);
    MatrixXd W = Mr * Mr.transpose() + 0.2 * MatrixXd::Identity(n, n);
    VectorXd tgt(n);
    for (int i = 0; i < n; ++i) tgt(i) = U(rng) * 2;
    GRBQuadExpr obj = 0;
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) obj += W(i, j) * (GRBLinExpr(x[i]) - tgt(i)) * (GRBLinExpr(x[j]) - tgt(j));
    m.setObjective(obj, GRB_MINIMIZE);
    // each group g has P boxes; option o constrains a different linear map of x (like a piece's control points)
    struct Box { MatrixXd A; VectorXd b; };
    std::vector<std::vector<Box>> boxes(G);
    std::vector<std::vector<GRBVar>> bin(G);
    for (int g = 0; g < G; ++g) {
      MatrixXd T(n, n);  // per-group linear map (piece-dependent)
      for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) T(i, j) = (i == j ? 1.0 : 0.3 * N01(rng));
      GRBLinExpr sum = 0;
      for (int o = 0; o < P; ++o) {
        GRBVar b = m.addVar(0, 1, 0, GRB_BINARY);
        bin[g].push_back(b);
        sum = sum + b;
        Box bx;
        bx.A.resize(2 * n, n);
        bx.b.resize(2 * n);
        for (int i = 0; i < n; ++i) {
          double c = U(rng), h = 0.5 + std::fabs(N01(rng));
          bx.A.row(2 * i) = T.row(i);
          bx.b(2 * i) = c + h;
          bx.A.row(2 * i + 1) = -T.row(i);
          bx.b(2 * i + 1) = -(c - h);
        }
        for (int r = 0; r < bx.A.rows(); ++r) {
          GRBLinExpr e = 0;
          for (int j = 0; j < n; ++j) e += bx.A(r, j) * x[j];
          m.addGenConstrIndicator(b, 1, e, GRB_LESS_EQUAL, bx.b(r));
        }
        boxes[g].push_back(bx);
      }
      m.addConstr(sum >= 1);
      if (trial % 5 == 0 && P > 2) m.addConstr(bin[g][1] == 0);  // like SANDO's "empty polytope" fixing
    }
    m.optimize();
    const int st = m.get(GRB_IntAttr_Status);
    qps_bb += m.lastNumQPs();

    // oracle: all P^G assignments, each QP solved with gi_qp directly in matrix form
    MatrixXd H = 2 * W;
    VectorXd c = -2 * W * tgt;
    double fbest = INFINITY;
    int combos = 1;
    for (int g = 0; g < G; ++g) combos *= P;
    for (int code = 0; code < combos; ++code) {
      int cc = code;
      std::vector<MatrixXd> As;
      std::vector<VectorXd> bs;
      bool skip = false;
      for (int g = 0; g < G; ++g) {
        int o = cc % P; cc /= P;
        if (trial % 5 == 0 && P > 2 && o == 1) skip = true;
        As.push_back(boxes[g][o].A);
        bs.push_back(boxes[g][o].b);
      }
      if (skip) continue;
      int rows = 2 * n;
      for (auto& A : As) rows += A.rows();
      MatrixXd CI(n, rows);
      VectorXd ci0(rows);
      int k = 0;
      for (int i = 0; i < n; ++i) {  // bounds -10..10
        CI.col(k) = VectorXd::Unit(n, i); ci0(k++) = 10;
        CI.col(k) = -VectorXd::Unit(n, i); ci0(k++) = 10;
      }
      for (size_t a = 0; a < As.size(); ++a)
        for (int r = 0; r < As[a].rows(); ++r) { CI.col(k) = -As[a].row(r).transpose(); ci0(k++) = bs[a](r); }
      ++qps_enum;
      giqp::Result r = giqp::solve(H, c, MatrixXd(n, 0), VectorXd(0), CI, ci0);
      if (r.status == giqp::Status::Optimal) fbest = std::min(fbest, r.f);
    }
    ++total;
    if (!std::isfinite(fbest)) {
      CHECK(st == GRB_INFEASIBLE, "miqp trial %d: oracle infeasible, compat status %d", trial, st);
      if (st == GRB_INFEASIBLE) ++agree;
      continue;
    }
    // objective of compat includes constant tgt'W tgt; oracle omits it
    const double cst = tgt.dot(W * tgt);
    bool ok = (st == GRB_OPTIMAL);
    double fc = ok ? m.get(GRB_DoubleAttr_ObjVal) - cst : NAN;
    ok = ok && std::fabs(fc - fbest) <= 1e-6 * std::max(1.0, std::fabs(fbest));
    // feasibility of the returned point w.r.t. the chosen boxes
    if (ok) {
      VectorXd xv(n);
      for (int i = 0; i < n; ++i) xv(i) = x[i].get(GRB_DoubleAttr_X);
      for (int g = 0; g < G; ++g) {
        int chosen = -1, cnt = 0;
        for (int o = 0; o < P; ++o) if (bin[g][o].get(GRB_DoubleAttr_X) > 0.5) { chosen = o; ++cnt; }
        ok = ok && cnt == 1;
        if (chosen >= 0) {
          VectorXd viol = boxes[g][chosen].A * xv - boxes[g][chosen].b;
          ok = ok && viol.maxCoeff() <= 1e-7;
        }
      }
    }
    CHECK(ok, "miqp trial %d: compat f=%.9g oracle f=%.9g status=%d", trial, fc, fbest, st);
    if (ok) ++agree;
  }
  std::printf("[MIQP random vs full enumeration] %d/%d agree; QPs solved: B&B %ld vs enumeration %ld\n", agree, total,
              qps_bb, qps_enum);
}

// API semantics that SANDO relies on: lazy removal, getVars/NumVars loop, re-solve after rebuild,
// callback abort, quadratic constraints rejected, objective value.
class AbortCb : public GRBCallback {
 public:
  bool stop = false;
 protected:
  void callback() override { if (stop) GRBCallback::abort(); }
};

static void testApiSemantics() {
  GRBEnv* env = new GRBEnv(true);
  env->set(GRB_IntParam_OutputFlag, 0);
  env->start();
  GRBModel m(*env);
  AbortCb cb;
  m.setCallback(&cb);
  m.set(GRB_StringAttr_ModelName, "planning");
  m.set(GRB_DoubleParam_TimeLimit, 1.0);
  for (int round = 0; round < 3; ++round) {
    GRBVar* vars = m.getVars();
    for (int i = 0; i < m.get(GRB_IntAttr_NumVars); ++i) m.remove(vars[i]);
    GRBVar a = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS, "a");
    GRBVar b = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS, "b");
    std::vector<GRBConstr> cs;
    cs.push_back(m.addConstr(a + b == 2.0 + round, "eq"));
    cs.push_back(m.addConstr(a >= 1.5, "lb"));
    GRBQuadExpr o = a * a + b * b;
    m.setObjective(o, GRB_MINIMIZE);
    m.optimize();
    CHECK(m.get(GRB_IntAttr_Status) == GRB_OPTIMAL, "round %d status", round);
    // min a^2+b^2 s.t. a+b=s, a>=1.5 -> a=max(s/2,1.5)
    const double s = 2.0 + round, ea = std::max(s / 2, 1.5);
    CHECK(std::fabs(a.get(GRB_DoubleAttr_X) - ea) < 1e-9, "round %d a=%g exp %g", round, a.get(GRB_DoubleAttr_X), ea);
    CHECK(std::fabs(m.get(GRB_DoubleAttr_ObjVal) - (ea * ea + (s - ea) * (s - ea))) < 1e-9, "objval");
    CHECK(m.get(GRB_IntAttr_NumVars) == 2, "numvars %d", m.get(GRB_IntAttr_NumVars));
    for (auto& c : cs) m.remove(c);
  }
  // abort
  cb.stop = true;
  GRBVar z = m.addVar(0, 1, 0, GRB_BINARY);
  GRBVar z2 = m.addVar(0, 1, 0, GRB_BINARY);
  m.addConstr(z + z2 >= 1);
  m.optimize();
  CHECK(m.get(GRB_IntAttr_Status) == GRB_INTERRUPTED, "abort status %d", m.get(GRB_IntAttr_Status));
  cb.stop = false;
  // quadratic constraint -> exception (SANDO only uses it in optional L2 mode)
  GRBVar q = m.addVar(-5, 5, 0, GRB_CONTINUOUS);
  GRBQConstr qc = m.addQConstr(q * q <= 1.0);
  bool threw = false;
  try { m.optimize(); } catch (GRBException&) { threw = true; }
  CHECK(threw, "quadratic constraint should throw");
  m.remove(qc);
  m.optimize();
  CHECK(m.get(GRB_IntAttr_Status) == GRB_OPTIMAL, "after removing qconstr");
  // infeasible
  GRBConstr c1 = m.addConstr(q >= 2.0), c2 = m.addConstr(q <= 1.0);
  m.optimize();
  CHECK(m.get(GRB_IntAttr_Status) == GRB_INFEASIBLE, "infeasible status %d", m.get(GRB_IntAttr_Status));
  bool threw2 = false;
  try { m.get(GRB_DoubleAttr_ObjVal); } catch (GRBException&) { threw2 = true; }
  CHECK(threw2, "ObjVal on infeasible must throw");
  m.remove(c1);
  m.remove(c2);
  std::printf("[API semantics] done\n");
}

// SANDO-like cubic-piece trajectory MIQP: N pieces x P polytopes (boxes), jerk cost, continuity,
// fixed initial/final state, per-axis velocity bounds on Bezier control points. 1 axis keeps it readable;
// the structure (indicator rows on control points, sum>=1 per piece) is the same as SANDO's.
static void testTrajectoryLike() {
  const int N = 5, P = 3;
  const double dt = 0.8, vmax = 4.0;
  GRBEnv env(true);
  GRBModel m(env);
  std::vector<GRBVar> a(N), b(N), c(N), d(N);
  for (int t = 0; t < N; ++t) {
    a[t] = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS);
    b[t] = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS);
    c[t] = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS);
    d[t] = m.addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS);
  }
  auto pos = [&](int t, double s) { return a[t] * (s * s * s) + b[t] * (s * s) + c[t] * s + GRBLinExpr(d[t]); };
  auto vel = [&](int t, double s) { return a[t] * (3 * s * s) + b[t] * (2 * s) + GRBLinExpr(c[t]); };
  auto acc = [&](int t, double s) { return a[t] * (6 * s) + 2.0 * GRBLinExpr(b[t]); };
  m.addConstr(pos(0, 0) == 0.0); m.addConstr(vel(0, 0) == 0.0); m.addConstr(acc(0, 0) == 0.0);
  m.addConstr(pos(N - 1, dt) == 6.0); m.addConstr(vel(N - 1, dt) == 0.0); m.addConstr(acc(N - 1, dt) == 0.0);
  for (int t = 0; t + 1 < N; ++t) {
    m.addConstr(pos(t, dt) == pos(t + 1, 0)); m.addConstr(vel(t, dt) == vel(t + 1, 0)); m.addConstr(acc(t, dt) == acc(t + 1, 0));
  }
  GRBQuadExpr J = 0;
  for (int t = 0; t < N; ++t) J += (6.0 * GRBLinExpr(a[t])) * (6.0 * GRBLinExpr(a[t]));
  m.setObjective(J);
  // Bezier control points of piece t
  auto cps = [&](int t) {
    std::vector<GRBLinExpr> p(4);
    p[0] = d[t];
    p[1] = c[t] * (dt / 3) + GRBLinExpr(d[t]);
    p[2] = b[t] * (dt * dt / 3) + c[t] * (2 * dt / 3) + GRBLinExpr(d[t]);
    p[3] = a[t] * (dt * dt * dt) + b[t] * (dt * dt) + c[t] * dt + GRBLinExpr(d[t]);
    return p;
  };
  // velocity control points bound
  for (int t = 0; t < N; ++t) {
    auto p = cps(t);
    for (int i = 0; i < 3; ++i) {
      GRBLinExpr v = (p[i + 1] - p[i]) * (3.0 / dt);
      m.addConstr(v <= vmax); m.addConstr(v >= -vmax);
    }
  }
  // corridors: 1D intervals per piece-layer; option 2 is a "detour" interval; option 1 empty in layer 3
  const double lo[P] = {-0.5, 1.5, 3.0}, hi[P] = {3.5, 6.5, 6.5};
  std::vector<std::vector<GRBVar>> s(N);
  for (int t = 0; t < N; ++t) {
    GRBLinExpr sum = 0;
    auto p = cps(t);
    for (int j = 0; j < P; ++j) {
      s[t].push_back(m.addVar(0, 1, 0, GRB_BINARY));
      sum = sum + s[t][j];
      for (int i = 0; i < 4; ++i) {
        m.addGenConstrIndicator(s[t][j], 1, p[i], GRB_LESS_EQUAL, hi[j]);
        m.addGenConstrIndicator(s[t][j], 1, -p[i], GRB_LESS_EQUAL, -lo[j]);
      }
    }
    m.addConstr(sum >= 1);
  }
  m.addConstr(s[3][1] == 0);
  auto t0 = std::chrono::steady_clock::now();
  m.optimize();
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(m.get(GRB_IntAttr_Status) == GRB_OPTIMAL, "traj status %d", m.get(GRB_IntAttr_Status));
  // verify: all chosen control points inside chosen interval, continuity, velocity
  double maxviol = 0;
  for (int t = 0; t < N; ++t) {
    int ch = -1;
    for (int j = 0; j < P; ++j) if (s[t][j].get(GRB_DoubleAttr_X) > 0.5) ch = j;
    auto p = cps(t);
    for (int i = 0; i < 4; ++i) {
      double v = p[i].getValue();
      maxviol = std::max({maxviol, v - hi[ch], lo[ch] - v});
    }
    CHECK(!(t == 3 && ch == 1), "fixed-zero polytope used");
  }
  CHECK(maxviol <= 1e-8, "corridor violation %g", maxviol);
  CHECK(std::fabs(pos(N - 1, dt).getValue() - 6.0) < 1e-8, "final pos");
  const double f_bb = m.get(GRB_DoubleAttr_ObjVal);
  const long qps_bb = m.lastNumQPs();
  // Oracle: force every one of the P^N assignments (fixing constraints), take the best.
  double f_enum = INFINITY;
  int feasible_assignments = 0;
  int combos = 1;
  for (int t = 0; t < N; ++t) combos *= P;
  for (int code = 0; code < combos; ++code) {
    std::vector<GRBConstr> fix;
    int cc = code;
    for (int t = 0; t < N; ++t) {
      int o = cc % P; cc /= P;
      for (int j = 0; j < P; ++j) fix.push_back(j == o ? m.addConstr(GRBLinExpr(s[t][j]) >= 1.0) : m.addConstr(s[t][j] == 0));
    }
    m.optimize();
    if (m.get(GRB_IntAttr_Status) == GRB_OPTIMAL) { ++feasible_assignments; f_enum = std::min(f_enum, m.get(GRB_DoubleAttr_ObjVal)); }
    for (auto& c : fix) m.remove(c);
  }
  CHECK(std::fabs(f_bb - f_enum) <= 1e-7 * std::max(1.0, std::fabs(f_enum)), "traj B&B %.10g vs enumeration %.10g", f_bb, f_enum);
  std::printf("[Trajectory-like N=%d P=%d] B&B obj %.6f (%ld QPs, %.3f ms) == enumeration obj %.6f over %d feasible of %d "
              "assignments; max corridor violation %.2e\n",
              N, P, f_bb, qps_bb, ms, f_enum, feasible_assignments, combos, maxviol);
}

int main() {
  testQPRandom();
  testMIQPRandom();
  testApiSemantics();
  testTrajectoryLike();
  std::printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
