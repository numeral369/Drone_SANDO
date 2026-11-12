// Dense strictly-convex QP solver: Goldfarb-Idnani dual active-set method.
//
//   minimize    0.5 x' G x + g0' x
//   subject to  CE' x + ce0  = 0
//               CI' x + ci0 >= 0
//
// Exact (active-set) method, well suited to small dense problems such as SANDO's
// per-assignment QPs (n = 6..30 variables, up to ~1000 constraints).
// Implementation follows Goldfarb & Idnani, "A numerically stable dual method for solving
// strictly convex quadratic programs", Math. Programming 27 (1983), in the structure popularised
// by QuadProg++ (L. Di Gaspero). Written from scratch for this project; MIT-style licence.
#pragma once
#include <Eigen/Dense>
#include <cmath>
#include <limits>
#include <vector>

namespace giqp {

enum class Status { Optimal, Infeasible, NotConvex, MaxIter };

struct Result {
  Status status = Status::Infeasible;
  Eigen::VectorXd x;
  double f = std::numeric_limits<double>::infinity();
  int iterations = 0;
  std::vector<int> active;  // indices of active inequality constraints at the solution
};

namespace detail {

inline double hyp(double a, double b) {
  const double a1 = std::fabs(a), b1 = std::fabs(b);
  if (a1 > b1) { const double t = b1 / a1; return a1 * std::sqrt(1.0 + t * t); }
  if (b1 > a1) { const double t = a1 / b1; return b1 * std::sqrt(1.0 + t * t); }
  return a1 * std::sqrt(2.0);
}

// Adds a constraint whose transformed normal is d to the factorisation (J, R).
inline bool addConstraint(Eigen::MatrixXd& R, Eigen::MatrixXd& J, Eigen::VectorXd& d, int& iq,
                          double& R_norm) {
  const int n = static_cast<int>(d.size());
  const double eps = std::numeric_limits<double>::epsilon();
  for (int j = n - 1; j >= iq + 1; --j) {
    double cc = d(j - 1), ss = d(j);
    const double h = hyp(cc, ss);
    if (std::fabs(h) < eps) continue;
    d(j) = 0.0;
    ss /= h;
    cc /= h;
    if (cc < 0.0) { cc = -cc; ss = -ss; d(j - 1) = -h; } else { d(j - 1) = h; }
    const double xny = ss / (1.0 + cc);
    for (int k = 0; k < n; ++k) {
      const double t1 = J(k, j - 1), t2 = J(k, j);
      J(k, j - 1) = t1 * cc + t2 * ss;
      J(k, j) = xny * (t1 + J(k, j - 1)) - t2;
    }
  }
  ++iq;
  for (int i = 0; i < iq; ++i) R(i, iq - 1) = d(i);
  if (std::fabs(d(iq - 1)) <= eps * R_norm) return false;  // linearly dependent
  R_norm = std::max(R_norm, std::fabs(d(iq - 1)));
  return true;
}

// Removes the active constraint with id l (A holds ids; equalities occupy the first p slots).
inline void deleteConstraint(Eigen::MatrixXd& R, Eigen::MatrixXd& J, std::vector<int>& A,
                             Eigen::VectorXd& u, int p, int& iq, int l) {
  const int n = static_cast<int>(R.rows());
  const double eps = std::numeric_limits<double>::epsilon();
  int qq = -1;
  for (int i = p; i < iq; ++i)
    if (A[i] == l) { qq = i; break; }
  if (qq < 0) return;
  for (int i = qq; i < iq - 1; ++i) {
    A[i] = A[i + 1];
    u(i) = u(i + 1);
    R.col(i) = R.col(i + 1);
  }
  A[iq - 1] = A[iq];
  u(iq - 1) = u(iq);
  A[iq] = 0;
  u(iq) = 0.0;
  for (int j = 0; j < iq; ++j) R(j, iq - 1) = 0.0;
  --iq;
  if (iq == 0) return;
  for (int j = qq; j < iq; ++j) {
    double cc = R(j, j), ss = R(j + 1, j);
    const double h = hyp(cc, ss);
    if (std::fabs(h) < eps) continue;
    cc /= h;
    ss /= h;
    R(j + 1, j) = 0.0;
    if (cc < 0.0) { R(j, j) = -h; cc = -cc; ss = -ss; } else { R(j, j) = h; }
    const double xny = ss / (1.0 + cc);
    for (int k = j + 1; k < iq; ++k) {
      const double t1 = R(j, k), t2 = R(j + 1, k);
      R(j, k) = t1 * cc + t2 * ss;
      R(j + 1, k) = xny * (t1 + R(j, k)) - t2;
    }
    for (int k = 0; k < n; ++k) {
      const double t1 = J(k, j), t2 = J(k, j + 1);
      J(k, j) = t1 * cc + t2 * ss;
      J(k, j + 1) = xny * (J(k, j) + t1) - t2;
    }
  }
}

}  // namespace detail

// CE: n x p, CI: n x m (constraints are columns).
inline Result solve(const Eigen::MatrixXd& G_in, const Eigen::VectorXd& g0, const Eigen::MatrixXd& CE,
                    const Eigen::VectorXd& ce0, const Eigen::MatrixXd& CI, const Eigen::VectorXd& ci0,
                    int max_iter = 10000) {
  using Eigen::MatrixXd;
  using Eigen::VectorXd;
  const int n = static_cast<int>(G_in.rows());
  const int p = static_cast<int>(CE.cols());
  const int m = static_cast<int>(CI.cols());
  const double inf = std::numeric_limits<double>::infinity();
  const double eps = std::numeric_limits<double>::epsilon();
  Result res;

  Eigen::LLT<MatrixXd> llt(G_in);
  if (llt.info() != Eigen::Success) { res.status = Status::NotConvex; return res; }
  const MatrixXd L = llt.matrixL();
  const double c1 = G_in.trace();
  // J = L^{-T}
  MatrixXd J = L.transpose().triangularView<Eigen::Upper>().solve(MatrixXd::Identity(n, n));
  const double c2 = J.trace();

  VectorXd x = -llt.solve(g0);
  double f = 0.5 * g0.dot(x);

  MatrixXd R = MatrixXd::Zero(n, n);
  VectorXd d(n), z(n), r(n + p + m + 1), u = VectorXd::Zero(p + m + 1);
  std::vector<int> A(p + m + 1, 0), A_old(p + m + 1, 0), iai(m, 0);
  std::vector<bool> iaexcl(m, true);
  VectorXd u_old(p + m + 1), x_old(n), s(m);
  int iq = 0;
  double R_norm = 1.0;

  auto update_z = [&](int iq_) {
    z.setZero();
    for (int j = iq_; j < n; ++j) z += J.col(j) * d(j);
  };
  auto update_r = [&](int iq_) {
    for (int i = iq_ - 1; i >= 0; --i) {
      double sum = 0.0;
      for (int j = i + 1; j < iq_; ++j) sum += R(i, j) * r(j);
      r(i) = (d(i) - sum) / R(i, i);
    }
  };

  // Equality constraints.
  for (int i = 0; i < p; ++i) {
    const VectorXd np = CE.col(i);
    d = J.transpose() * np;
    update_z(iq);
    update_r(iq);
    double t2 = 0.0;
    if (std::fabs(z.dot(z)) > eps) t2 = (-np.dot(x) - ce0(i)) / z.dot(np);
    x += t2 * z;
    u(iq) = t2;
    for (int k = 0; k < iq; ++k) u(k) -= t2 * r(k);
    f += 0.5 * t2 * t2 * z.dot(np);
    A[i] = -i - 1;
    if (!detail::addConstraint(R, J, d, iq, R_norm)) { res.status = Status::Infeasible; return res; }
  }
  for (int i = 0; i < m; ++i) iai[i] = i;

  int iter = 0;
  int ip = 0;
  double ss = 0.0;
  VectorXd np(n);

  // Main loop: Step 1 (pick violated constraint) / Step 2 (add it, dropping others as needed).
  while (true) {
    if (++iter > max_iter) { res.status = Status::MaxIter; return res; }
    for (int i = p; i < iq; ++i) iai[A[i]] = -1;
    double psi = 0.0;
    for (int i = 0; i < m; ++i) {
      iaexcl[i] = true;
      s(i) = CI.col(i).dot(x) + ci0(i);
      psi += std::min(0.0, s(i));
    }
    if (std::fabs(psi) <= m * eps * c1 * c2 * 100.0) break;  // all satisfied
    u_old.head(iq) = u.head(iq);
    for (int i = 0; i < iq; ++i) A_old[i] = A[i];
    x_old = x;

    bool restart_step1 = false;
    while (!restart_step1) {  // l2
      ss = 0.0;
      ip = -1;
      for (int i = 0; i < m; ++i)
        if (s(i) < ss && iai[i] != -1 && iaexcl[i]) { ss = s(i); ip = i; }
      if (ip < 0) {  // no violated constraint left
        goto done;
      }
      np = CI.col(ip);
      u(iq) = 0.0;
      A[iq] = ip;

      while (true) {  // l2a
        if (++iter > max_iter) { res.status = Status::MaxIter; return res; }
        d = J.transpose() * np;
        update_z(iq);
        update_r(iq);
        int l = 0;
        double t1 = inf;
        for (int k = p; k < iq; ++k)
          if (r(k) > 0.0 && u(k) / r(k) < t1) { t1 = u(k) / r(k); l = A[k]; }
        double t2 = inf;
        if (std::fabs(z.dot(z)) > eps) t2 = -s(ip) / z.dot(np);
        const double t = std::min(t1, t2);
        if (t >= inf) { res.status = Status::Infeasible; return res; }
        if (t2 >= inf) {  // step in dual space only
          for (int k = 0; k < iq; ++k) u(k) -= t * r(k);
          u(iq) += t;
          iai[l] = l;
          detail::deleteConstraint(R, J, A, u, p, iq, l);
          continue;
        }
        x += t * z;
        f += t * z.dot(np) * (0.5 * t + u(iq));
        for (int k = 0; k < iq; ++k) u(k) -= t * r(k);
        u(iq) += t;
        if (std::fabs(t - t2) < eps) {  // full step: add constraint ip
          if (!detail::addConstraint(R, J, d, iq, R_norm)) {
            iaexcl[ip] = false;
            detail::deleteConstraint(R, J, A, u, p, iq, ip);
            for (int i = 0; i < m; ++i) iai[i] = i;
            for (int i = p; i < iq; ++i) {
              A[i] = A_old[i];
              u(i) = u_old(i);
              iai[A[i]] = -1;
            }
            x = x_old;
            break;  // back to l2
          }
          iai[ip] = -1;
          restart_step1 = true;
          break;
        }
        // partial step: drop constraint l
        iai[l] = l;
        detail::deleteConstraint(R, J, A, u, p, iq, l);
        s(ip) = CI.col(ip).dot(x) + ci0(ip);
      }
    }
  }
done:
  res.status = Status::Optimal;
  res.x = x;
  res.f = 0.5 * x.dot(G_in * x) + g0.dot(x);
  res.iterations = iter;
  for (int i = p; i < iq; ++i) res.active.push_back(A[i]);
  return res;
}

}  // namespace giqp
