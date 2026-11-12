// grb_compat: open-source, source-compatible replacement for the subset of the Gurobi C++ API
// used by SANDO (mit-acl/sando, src/sando/gurobi_solver.cpp).
//
// NOT affiliated with or derived from Gurobi Optimization. Only the public API names that SANDO
// calls are mirrored so that SANDO compiles unchanged. The solver behind it is:
//   * exact enumeration / branch-and-bound over the binary "piece -> polytope" choices, and
//   * an exact dense convex QP solver (Goldfarb-Idnani, gi_qp.hpp) for each choice.
// Supported model class: convex QP objective over continuous variables, linear constraints,
// binary variables that appear only in binary-only linear constraints and in indicator
// constraints. Anything else raises GRBException at optimize().
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#define GRB_INFINITY 1e100
#define GRB_CONTINUOUS 'C'
#define GRB_BINARY 'B'
#define GRB_INTEGER 'I'
#define GRB_LESS_EQUAL '<'
#define GRB_GREATER_EQUAL '>'
#define GRB_EQUAL '='
#define GRB_MINIMIZE 1
#define GRB_MAXIMIZE -1

#define GRB_LOADED 1
#define GRB_OPTIMAL 2
#define GRB_INFEASIBLE 3
#define GRB_INF_OR_UNBD 4
#define GRB_UNBOUNDED 5
#define GRB_TIME_LIMIT 9
#define GRB_INTERRUPTED 11
#define GRB_NUMERIC 12

enum GRB_IntParam { GRB_IntParam_OutputFlag, GRB_IntParam_LogToConsole, GRB_IntParam_Threads };
enum GRB_DoubleParam { GRB_DoubleParam_TimeLimit };
enum GRB_StringParam { GRB_StringParam_LogFile };
enum GRB_IntAttr { GRB_IntAttr_Status, GRB_IntAttr_NumVars };
enum GRB_DoubleAttr { GRB_DoubleAttr_X, GRB_DoubleAttr_Runtime, GRB_DoubleAttr_ObjVal };
enum GRB_StringAttr { GRB_StringAttr_ModelName };

class GRBException {
 public:
  explicit GRBException(std::string msg = "", int code = 10000) : msg_(std::move(msg)), code_(code) {}
  const std::string& getMessage() const { return msg_; }
  int getErrorCode() const { return code_; }

 private:
  std::string msg_;
  int code_;
};

namespace grbc {
struct VarData {
  double lb, ub;
  char type;
  std::string name;
  double x = 0.0;
  bool removed = false;
  long id = -1;  // index within the model's committed list (rebuilt at update)
};
struct ModelImpl;
}  // namespace grbc

class GRBVar {
 public:
  GRBVar() = default;
  explicit GRBVar(std::shared_ptr<grbc::VarData> d) : d_(std::move(d)) {}
  double get(GRB_DoubleAttr a) const;
  std::string get(GRB_StringAttr) const { return d_ ? d_->name : std::string(); }
  const std::shared_ptr<grbc::VarData>& data() const { return d_; }
  bool sameAs(const GRBVar& o) const { return d_ == o.d_; }

 private:
  std::shared_ptr<grbc::VarData> d_;
};

class GRBLinExpr {
 public:
  struct Term {
    std::shared_ptr<grbc::VarData> v;
    double c;
  };
  GRBLinExpr(double constant = 0.0) : k_(constant) {}  // NOLINT (implicit by design)
  GRBLinExpr(const GRBVar& v, double coef = 1.0) : k_(0.0) {  // NOLINT
    if (v.data()) t_.push_back({v.data(), coef});
  }
  GRBLinExpr& operator+=(const GRBLinExpr& o);
  GRBLinExpr& operator-=(const GRBLinExpr& o);
  GRBLinExpr& operator*=(double s);
  GRBLinExpr& operator/=(double s) { return (*this) *= (1.0 / s); }
  GRBLinExpr operator-() const {
    GRBLinExpr r(*this);
    r *= -1.0;
    return r;
  }
  double getConstant() const { return k_; }
  const std::vector<Term>& terms() const { return t_; }
  double getValue() const;
  unsigned int size() const { return static_cast<unsigned int>(t_.size()); }
  void compress();  // merge duplicate variables

 private:
  double k_;
  std::vector<Term> t_;
};

class GRBQuadExpr {
 public:
  struct QTerm {
    std::shared_ptr<grbc::VarData> a, b;
    double c;
  };
  GRBQuadExpr(double constant = 0.0) : lin_(constant) {}  // NOLINT
  GRBQuadExpr(const GRBLinExpr& l) : lin_(l) {}           // NOLINT
  GRBQuadExpr& operator+=(const GRBQuadExpr& o);
  GRBQuadExpr& operator-=(const GRBQuadExpr& o);
  GRBQuadExpr& operator*=(double s);
  const GRBLinExpr& linear() const { return lin_; }
  const std::vector<QTerm>& qterms() const { return q_; }
  void addQTerm(const std::shared_ptr<grbc::VarData>& a, const std::shared_ptr<grbc::VarData>& b,
                double c) {
    q_.push_back({a, b, c});
  }
  double getValue() const;

 private:
  GRBLinExpr lin_;
  std::vector<QTerm> q_;
};

GRBLinExpr operator+(const GRBLinExpr& a, const GRBLinExpr& b);
GRBLinExpr operator-(const GRBLinExpr& a, const GRBLinExpr& b);
GRBLinExpr operator*(double s, const GRBLinExpr& a);
GRBLinExpr operator*(const GRBLinExpr& a, double s);
GRBLinExpr operator/(const GRBLinExpr& a, double s);
GRBQuadExpr operator*(const GRBLinExpr& a, const GRBLinExpr& b);
GRBQuadExpr operator+(const GRBQuadExpr& a, const GRBQuadExpr& b);
GRBQuadExpr operator-(const GRBQuadExpr& a, const GRBQuadExpr& b);
GRBQuadExpr operator*(double s, const GRBQuadExpr& a);
GRBQuadExpr operator*(const GRBQuadExpr& a, double s);
// Disambiguation for "Lin op double" and "Var op Var" etc.
inline GRBLinExpr operator+(const GRBLinExpr& a, double b) { return a + GRBLinExpr(b); }
inline GRBLinExpr operator+(double a, const GRBLinExpr& b) { return GRBLinExpr(a) + b; }
inline GRBLinExpr operator-(const GRBLinExpr& a, double b) { return a - GRBLinExpr(b); }
inline GRBLinExpr operator-(double a, const GRBLinExpr& b) { return GRBLinExpr(a) - b; }
inline GRBLinExpr operator-(const GRBVar& v) { return -GRBLinExpr(v); }
inline GRBLinExpr operator*(double s, const GRBVar& v) { return GRBLinExpr(v, s); }
inline GRBLinExpr operator*(const GRBVar& v, double s) { return GRBLinExpr(v, s); }
inline GRBQuadExpr operator*(const GRBVar& a, const GRBVar& b) { return GRBLinExpr(a) * GRBLinExpr(b); }

class GRBTempConstr {
 public:
  GRBQuadExpr expr;  // expr (sense) 0
  char sense = GRB_LESS_EQUAL;
  bool quadratic = false;
};
GRBTempConstr operator<=(const GRBLinExpr& a, const GRBLinExpr& b);
GRBTempConstr operator>=(const GRBLinExpr& a, const GRBLinExpr& b);
GRBTempConstr operator==(const GRBLinExpr& a, const GRBLinExpr& b);
GRBTempConstr operator<=(const GRBLinExpr& a, double b);
GRBTempConstr operator>=(const GRBLinExpr& a, double b);
GRBTempConstr operator==(const GRBLinExpr& a, double b);
GRBTempConstr operator<=(const GRBQuadExpr& a, double b);
GRBTempConstr operator>=(const GRBQuadExpr& a, double b);
inline GRBTempConstr operator<=(const GRBVar& a, double b) { return GRBLinExpr(a) <= b; }
inline GRBTempConstr operator>=(const GRBVar& a, double b) { return GRBLinExpr(a) >= b; }
inline GRBTempConstr operator==(const GRBVar& a, double b) { return GRBLinExpr(a) == b; }
inline GRBTempConstr operator<=(const GRBVar& a, int b) { return GRBLinExpr(a) <= double(b); }
inline GRBTempConstr operator>=(const GRBVar& a, int b) { return GRBLinExpr(a) >= double(b); }
inline GRBTempConstr operator==(const GRBVar& a, int b) { return GRBLinExpr(a) == double(b); }
inline GRBTempConstr operator<=(const GRBLinExpr& a, int b) { return a <= double(b); }
inline GRBTempConstr operator>=(const GRBLinExpr& a, int b) { return a >= double(b); }
inline GRBTempConstr operator==(const GRBLinExpr& a, int b) { return a == double(b); }

namespace grbc {
struct ConstrData {
  GRBLinExpr expr;  // expr (sense) rhs, rhs folded into the constant: expr (sense) 0
  char sense = GRB_LESS_EQUAL;
  std::string name;
  bool removed = false;
  // indicator constraints:
  bool indicator = false;
  std::shared_ptr<VarData> binvar;
  int binval = 1;
  // quadratic constraints (unsupported by the solver, kept for removal semantics):
  bool quadratic = false;
};
}  // namespace grbc

class GRBConstr {
 public:
  GRBConstr() = default;
  explicit GRBConstr(std::shared_ptr<grbc::ConstrData> d) : d_(std::move(d)) {}
  const std::shared_ptr<grbc::ConstrData>& data() const { return d_; }

 private:
  std::shared_ptr<grbc::ConstrData> d_;
};
class GRBGenConstr : public GRBConstr {
 public:
  using GRBConstr::GRBConstr;
};
class GRBQConstr : public GRBConstr {
 public:
  using GRBConstr::GRBConstr;
};

class GRBEnv {
 public:
  explicit GRBEnv(bool /*empty*/ = false) {}
  void set(GRB_IntParam, int) {}
  void set(GRB_DoubleParam, double) {}
  void set(GRB_StringParam, const std::string&) {}
  void start() {}
};

class GRBCallback {
 public:
  virtual ~GRBCallback() = default;

 protected:
  GRBCallback() = default;
  virtual void callback() {}
  void abort() { aborted_ = true; }
  int where = 0;

 private:
  friend struct grbc::ModelImpl;
  std::atomic<bool> aborted_{false};
};

class GRBModel {
 public:
  explicit GRBModel(const GRBEnv& env);
  GRBModel(const GRBModel&) = delete;
  GRBModel(GRBModel&&) noexcept;
  GRBModel& operator=(GRBModel&&) noexcept;
  ~GRBModel();

  GRBVar addVar(double lb, double ub, double obj, char type, const std::string& name = "");
  GRBConstr addConstr(const GRBTempConstr& tc, const std::string& name = "");
  GRBConstr addConstr(const GRBLinExpr& lhs, char sense, double rhs, const std::string& name = "");
  GRBGenConstr addGenConstrIndicator(const GRBVar& binvar, int binval, const GRBLinExpr& expr,
                                     char sense, double rhs, const std::string& name = "");
  GRBGenConstr addGenConstrIndicator(const GRBVar& binvar, int binval, const GRBTempConstr& tc,
                                     const std::string& name = "");
  GRBQConstr addQConstr(const GRBTempConstr& tc, const std::string& name = "");
  void remove(const GRBVar& v);
  void remove(const GRBConstr& c);
  void setObjective(const GRBQuadExpr& obj, int sense = GRB_MINIMIZE);
  void setObjective(const GRBLinExpr& obj, int sense = GRB_MINIMIZE);
  void setCallback(GRBCallback* cb);
  void update();
  void optimize();
  void reset() {}
  GRBVar* getVars();

  void set(GRB_IntParam p, int v);
  void set(GRB_DoubleParam p, double v);
  void set(GRB_StringParam, const std::string&) {}
  void set(GRB_StringAttr, const std::string& v);
  int get(GRB_IntAttr a) const;
  double get(GRB_DoubleAttr a) const;

  // grb_compat extensions (diagnostics)
  long lastNumQPs() const;
  long lastNumLeaves() const;

 private:
  std::unique_ptr<grbc::ModelImpl> impl_;
};
