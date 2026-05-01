#include "types.h"
#include <stdlib.h>
#include <string.h>

/* ========================= small utils ========================= */

static char *dup_cstr(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *d = (char *)malloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

static const char *after_colon(const char *label) {
  if (!label) return "";
  const char *c = strchr(label, ':');
  return c ? (c + 1) : label;
}

static int is_token_kind(const ASTNode *n, const char *kind) {
  if (!n || !n->label || !kind) return 0;
  size_t k = strlen(kind);
  return (strncmp(n->label, kind, k) == 0 && n->label[k] == ':');
}

static int streq(const char *a, const char *b) {
  if (a == b) return 1;
  if (!a || !b) return 0;
  return strcmp(a, b) == 0;
}

static char *extract_type_name(const ASTNode *type_node) {
  if (!type_node || !type_node->label) return dup_cstr("void");
  if (strchr(type_node->label, ':')) return dup_cstr(after_colon(type_node->label));

  /* часто typeRef -> child leaf */
  if (type_node->numChildren > 0) {
    for (int i = 0; i < type_node->numChildren; i++) {
      const ASTNode *c = type_node->children[i];
      if (c && c->label && strchr(c->label, ':')) {
        return dup_cstr(after_colon(c->label));
      }
    }
  }

  return dup_cstr(type_node->label);
}

static int is_class_node(const ASTNode *n) {
  if (!n || !n->label) return 0;
  return streq(n->label, "class") || streq(n->label, "classDef");
}

static int is_func_node(const ASTNode *n) {
  if (!n || !n->label) return 0;
  return streq(n->label, "funcDef") || streq(n->label, "funcDecl") ||
         streq(n->label, "methodDef") || streq(n->label, "methodDecl");
}

/* Ищем среди прямых детей токен kind:* */
static const ASTNode *find_child_token(const ASTNode *n, const char *kind) {
  if (!n || !kind) return NULL;
  for (int i = 0; i < n->numChildren; i++) {
    const ASTNode *c = n->children[i];
    if (c && is_token_kind(c, kind)) return c;
  }
  return NULL;
}

/* Ищем в одном уровне ребёнка с label == name */
static const ASTNode *find_child_label(const ASTNode *n, const char *label) {
  if (!n || !label) return NULL;
  for (int i = 0; i < n->numChildren; i++) {
    const ASTNode *c = n->children[i];
    if (c && c->label && streq(c->label, label)) return c;
  }
  return NULL;
}

/* Ищем сигнатуру у func/method ноды (обычно child[0] == "signature") */
static const ASTNode *find_signature_node(const ASTNode *fn) {
  if (!fn) return NULL;
  for (int i = 0; i < fn->numChildren; i++) {
    const ASTNode *c = fn->children[i];
    if (c && c->label && streq(c->label, "signature")) return c;
  }
  return NULL;
}

static char *make_impl_label(const char *class_name, const char *method_name) {
  if (!class_name || !method_name) return dup_cstr("unknown");
  size_t a = strlen(class_name), b = strlen(method_name);
  size_t n = a + 2 + b + 1; /* Class__method */
  char *s = (char *)malloc(n);
  if (!s) return NULL;
  memcpy(s, class_name, a);
  s[a] = '_';
  s[a + 1] = '_';
  memcpy(s + a + 2, method_name, b);
  s[a + 2 + b] = '\0';
  return s;
}

/* ========================= env structs ========================= */

struct TypeEnv {
  ClassInfo **classes;
  int n, cap;
};

/* Временные declared-списки (то, что объявлено в самом классе, без наследования) */
typedef struct {
  ClassInfo *ci;

  FieldInfo *decl_fields;
  int n_decl_fields, cap_decl_fields;

  MethodInfo *decl_methods;
  int n_decl_methods, cap_decl_methods;

  int visiting;
  int done;
} ClassBuild;

typedef struct {
  TypeEnv *env;
  ClassBuild **builds;
  int n_builds;
} BuildCtx;

/* ========================= dyn arrays ========================= */

static void env_add_class(TypeEnv *env, ClassInfo *ci) {
  if (!env || !ci) return;
  if (env->n == env->cap) {
    int nc = env->cap ? env->cap * 2 : 16;
    void *np = realloc(env->classes, (size_t)nc * sizeof(env->classes[0]));
    if (!np) return;
    env->classes = (ClassInfo **)np;
    env->cap = nc;
  }
  env->classes[env->n++] = ci;
}

static void cb_add_decl_field(ClassBuild *cb, const char *name, const char *type_name) {
  if (!cb || !name) return;
  if (cb->n_decl_fields == cb->cap_decl_fields) {
    int nc = cb->cap_decl_fields ? cb->cap_decl_fields * 2 : 16;
    void *np = realloc(cb->decl_fields, (size_t)nc * sizeof(cb->decl_fields[0]));
    if (!np) return;
    cb->decl_fields = (FieldInfo *)np;
    cb->cap_decl_fields = nc;
  }
  FieldInfo *f = &cb->decl_fields[cb->n_decl_fields++];
  memset(f, 0, sizeof(*f));
  f->name = dup_cstr(name);
  f->type_name = dup_cstr(type_name ? type_name : "void");
  f->offset = 0; /* заполним на этапе layout */
}

static void cb_add_decl_method(ClassBuild *cb,
                               const char *name,
                               const char *ret_type,
                               const char *impl_label) {
  if (!cb || !name) return;
  if (cb->n_decl_methods == cb->cap_decl_methods) {
    int nc = cb->cap_decl_methods ? cb->cap_decl_methods * 2 : 16;
    void *np = realloc(cb->decl_methods, (size_t)nc * sizeof(cb->decl_methods[0]));
    if (!np) return;
    cb->decl_methods = (MethodInfo *)np;
    cb->cap_decl_methods = nc;
  }
  MethodInfo *m = &cb->decl_methods[cb->n_decl_methods++];
  memset(m, 0, sizeof(*m));
  m->name = dup_cstr(name);
  m->ret_type = dup_cstr(ret_type ? ret_type : "void");
  m->slot = -1; /* заполним на этапе vtable */
  m->impl_label = dup_cstr(impl_label ? impl_label : "unknown");
}

static void free_fieldinfo_array(FieldInfo *arr, int n) {
  if (!arr) return;
  for (int i = 0; i < n; i++) {
    free(arr[i].name);
    free(arr[i].type_name);
  }
  free(arr);
}

static void free_methodinfo_array(MethodInfo *arr, int n) {
  if (!arr) return;
  for (int i = 0; i < n; i++) {
    free(arr[i].name);
    free(arr[i].ret_type);
    free(arr[i].impl_label);
  }
  free(arr);
}

/* ========================= lookup helpers ========================= */

static ClassBuild *ctx_find_build(BuildCtx *ctx, const char *class_name) {
  if (!ctx || !class_name) return NULL;
  for (int i = 0; i < ctx->n_builds; i++) {
    ClassBuild *cb = ctx->builds[i];
    if (cb && cb->ci && cb->ci->name && streq(cb->ci->name, class_name)) return cb;
  }
  return NULL;
}

const ClassInfo *types_find_class(const TypeEnv *env, const char *name) {
  if (!env || !name) return NULL;
  for (int i = 0; i < env->n; i++) {
    ClassInfo *c = env->classes[i];
    if (c && c->name && streq(c->name, name)) return c;
  }
  return NULL;
}

/* ========================= AST extraction ========================= */

/* Вытаскиваем имя класса:
   - пробуем прямого ребёнка id:*
   - иначе NULL */
static char *extract_class_name(const ASTNode *class_node) {
  const ASTNode *id = find_child_token(class_node, "id");
  if (id) return dup_cstr(after_colon(id->label));
  /* иногда парсер делает kind IDENTIFIER -> "IDENTIFIER:Name" */
  id = find_child_token(class_node, "IDENTIFIER");
  if (id) return dup_cstr(after_colon(id->label));
  return NULL;
}

/* Вытаскиваем base:
   - пробуем leaf base:Base
   - пробуем child label "base" -> внутри id:* */
static char *extract_base_name(const ASTNode *class_node) {
  const ASTNode *b = find_child_token(class_node, "base");
  if (b) return dup_cstr(after_colon(b->label));

  const ASTNode *base_node = find_child_label(class_node, "base");
  if (base_node) {
    const ASTNode *id = find_child_token(base_node, "id");
    if (id) return dup_cstr(after_colon(id->label));
    id = find_child_token(base_node, "IDENTIFIER");
    if (id) return dup_cstr(after_colon(id->label));
  }

  /* иногда label "extends" */
  const ASTNode *ext = find_child_label(class_node, "extends");
  if (ext) {
    const ASTNode *id = find_child_token(ext, "id");
    if (id) return dup_cstr(after_colon(id->label));
    id = find_child_token(ext, "IDENTIFIER");
    if (id) return dup_cstr(after_colon(id->label));
  }

  return NULL;
}

/* Собираем поля из vardecl: typeRef vars */
static void collect_fields_from_vardecl(ClassBuild *cb, const ASTNode *vardecl) {
  if (!cb || !vardecl) return;
  if (vardecl->numChildren < 2) return;

  const ASTNode *type_node = vardecl->children[0];
  const ASTNode *vars = vardecl->children[1];

  char *type_name = extract_type_name(type_node);
  if (!vars || !vars->label || !streq(vars->label, "vars")) {
    free(type_name);
    return;
  }

  /* vars: id, optAssign, id, optAssign ... */
  for (int i = 0; i < vars->numChildren; i += 2) {
    const ASTNode *idn = vars->children[i];
    if (!idn || !idn->label) continue;

    const char *nm = NULL;
    if (is_token_kind(idn, "id") || is_token_kind(idn, "IDENTIFIER")) {
      nm = after_colon(idn->label);
    }
    if (nm && *nm) cb_add_decl_field(cb, nm, type_name);
  }

  free(type_name);
}

/* Собираем метод из funcDef/funcDecl (или methodDef/methodDecl):
   signature: [0]=retType, [1]=id, [2]=args... (в твоих модулях так) */
static void collect_method_from_func(ClassBuild *cb, const ASTNode *fn) {
  if (!cb || !fn || !cb->ci || !cb->ci->name) return;

  const ASTNode *sig = find_signature_node(fn);
  if (!sig || sig->numChildren < 2) return;

  const ASTNode *ret = sig->children[0];
  const ASTNode *idn = sig->children[1];

  if (!idn || !idn->label) return;

  const char *mname = NULL;
  if (is_token_kind(idn, "id") || is_token_kind(idn, "IDENTIFIER")) {
    mname = after_colon(idn->label);
  } else if (idn->label && strchr(idn->label, ':')) {
    mname = after_colon(idn->label);
  }

  if (!mname || !*mname) return;

  char *ret_type = extract_type_name(ret);
  char *impl = make_impl_label(cb->ci->name, mname);

  cb_add_decl_method(cb, mname, ret_type, impl);

  free(ret_type);
  free(impl);
}

/* Рекурсивно собираем члены класса, но:
   - если встретили func/method — собираем сигнатуру и НЕ заходим в тело
   - если встретили vardecl — собираем поля и НЕ идём глубже (локальные init не нужны)
*/
static void collect_members_from_node(ClassBuild *cb, const ASTNode *n) {
  if (!cb || !n || !n->label) return;

  if (is_func_node(n)) {
    collect_method_from_func(cb, n);
    return; /* не лезем внутрь тела */
  }

  if (streq(n->label, "vardecl") || streq(n->label, "fieldDecl") || streq(n->label, "field")) {
    /* если это не vardecl-форма — всё равно попробуем как vardecl, если структура похожа */
    collect_fields_from_vardecl(cb, n);
    return;
  }

  /* wrappers типа public/private — просто рекурсивно */
  for (int i = 0; i < n->numChildren; i++) {
    collect_members_from_node(cb, n->children[i]);
  }
}

/* ========================= layout + vtable ========================= */

static int find_method_slot_by_name(const MethodInfo *vt, int n, const char *name) {
  if (!vt || !name) return -1;
  for (int i = 0; i < n; i++) {
    if (vt[i].name && streq(vt[i].name, name)) return i;
  }
  return -1;
}

static void compute_layout(BuildCtx *ctx, ClassBuild *cb) {
  if (!ctx || !cb || !cb->ci) return;
  if (cb->done) return;

  if (cb->visiting) {
    /* цикл наследования — режем */
    cb->ci->base = NULL;
    cb->ci->base_name = NULL;
    cb->done = 1;
    cb->visiting = 0;
    return;
  }

  cb->visiting = 1;

  /* 1) вычисляем базовый */
  const ClassInfo *base_ci = NULL;
  if (cb->ci->base_name) {
    ClassBuild *base_cb = ctx_find_build(ctx, cb->ci->base_name);
    if (base_cb) {
      compute_layout(ctx, base_cb);
      cb->ci->base = base_cb->ci;
      base_ci = base_cb->ci;
    } else {
      cb->ci->base = NULL;
      base_ci = NULL;
    }
  }

  /* 2) поля: копируем унаследованные + добавляем свои */
  int inherited_fields = base_ci ? base_ci->n_fields : 0;
  int total_fields = inherited_fields + cb->n_decl_fields;

  FieldInfo *fields = NULL;
  if (total_fields > 0) {
    fields = (FieldInfo *)calloc((size_t)total_fields, sizeof(FieldInfo));
    if (!fields) {
      cb->visiting = 0;
      cb->done = 1;
      return;
    }
  }

  /* копия inherited */
  for (int i = 0; i < inherited_fields; i++) {
    fields[i].name = dup_cstr(base_ci->fields[i].name);
    fields[i].type_name = dup_cstr(base_ci->fields[i].type_name);
    fields[i].offset = base_ci->fields[i].offset;
  }

  int off = base_ci ? base_ci->size_bytes : 8; /* 0..7 = vptr */
  if (off < 8) off = 8;

  for (int i = 0; i < cb->n_decl_fields; i++) {
    FieldInfo *dst = &fields[inherited_fields + i];
    dst->name = dup_cstr(cb->decl_fields[i].name);
    dst->type_name = dup_cstr(cb->decl_fields[i].type_name);
    dst->offset = off;
    off += 8; /* упрощение: каждое поле 8 байт */
  }

  /* 3) vtable: копируем базовую + override/append */
  int inherited_slots = base_ci ? base_ci->n_slots : 0;
  int max_slots = inherited_slots + cb->n_decl_methods;

  MethodInfo *vt = NULL;
  if (max_slots > 0) {
    vt = (MethodInfo *)calloc((size_t)max_slots, sizeof(MethodInfo));
    if (!vt) {
      free_fieldinfo_array(fields, total_fields);
      cb->visiting = 0;
      cb->done = 1;
      return;
    }
  }

  int nslots = inherited_slots;
  for (int i = 0; i < inherited_slots; i++) {
    vt[i].name = dup_cstr(base_ci->vtable[i].name);
    vt[i].ret_type = dup_cstr(base_ci->vtable[i].ret_type);
    vt[i].slot = base_ci->vtable[i].slot;
    vt[i].impl_label = dup_cstr(base_ci->vtable[i].impl_label);
  }

  for (int i = 0; i < cb->n_decl_methods; i++) {
    const char *mname = cb->decl_methods[i].name;
    int idx = find_method_slot_by_name(vt, nslots, mname);
    if (idx >= 0) {
      /* override: слот сохраняем, impl заменяем */
      free(vt[idx].ret_type);
      free(vt[idx].impl_label);
      vt[idx].ret_type = dup_cstr(cb->decl_methods[i].ret_type);
      vt[idx].impl_label = dup_cstr(cb->decl_methods[i].impl_label);
      /* имя оставляем тем же */
    } else {
      /* append new method */
      vt[nslots].name = dup_cstr(cb->decl_methods[i].name);
      vt[nslots].ret_type = dup_cstr(cb->decl_methods[i].ret_type);
      vt[nslots].slot = nslots;
      vt[nslots].impl_label = dup_cstr(cb->decl_methods[i].impl_label);
      nslots++;
    }
  }

  /* 4) сохраняем в ClassInfo */
  /* если вдруг пересборка — подчистим старое */
  if (cb->ci->fields) free_fieldinfo_array(cb->ci->fields, cb->ci->n_fields);
  if (cb->ci->vtable) free_methodinfo_array(cb->ci->vtable, cb->ci->n_slots);

  cb->ci->fields = fields;
  cb->ci->n_fields = total_fields;

  cb->ci->vtable = vt;
  cb->ci->n_slots = nslots;

  cb->ci->size_bytes = off;
  if (cb->ci->size_bytes < 8) cb->ci->size_bytes = 8;

  cb->visiting = 0;
  cb->done = 1;
}

/* ========================= class collection ========================= */

static void builds_add(BuildCtx *ctx, ClassBuild *cb) {
  if (!ctx || !cb) return;
  int n = ctx->n_builds;
  ClassBuild **nb = (ClassBuild **)realloc(ctx->builds, (size_t)(n + 1) * sizeof(ctx->builds[0]));
  if (!nb) return;
  ctx->builds = nb;
  ctx->builds[n] = cb;
  ctx->n_builds = n + 1;
}

/* Берём "контейнер членов": если есть child с label "members"/"memberList"/"membersList" — используем.
   Иначе — просто рекурсивно по всему class_node, но осторожно:
   мы собираем только vardecl/func на верхних уровнях, а внутрь методов не полезем. */
static const ASTNode *pick_members_container(const ASTNode *class_node) {
  const ASTNode *m = NULL;
  m = find_child_label(class_node, "members");
  if (m) return m;
  m = find_child_label(class_node, "memberList");
  if (m) return m;
  m = find_child_label(class_node, "membersList");
  if (m) return m;
  m = find_child_label(class_node, "classMembers");
  if (m) return m;
  return NULL;
}

static void collect_one_class(BuildCtx *ctx, const ASTNode *class_node) {
  if (!ctx || !class_node) return;

  char *cname = extract_class_name(class_node);
  if (!cname || !*cname) {
    free(cname);
    return;
  }

  ClassInfo *ci = (ClassInfo *)calloc(1, sizeof(ClassInfo));
  if (!ci) {
    free(cname);
    return;
  }

  ci->name = cname;
  ci->base_name = extract_base_name(class_node);
  ci->base = NULL;
  ci->fields = NULL;
  ci->n_fields = 0;
  ci->vtable = NULL;
  ci->n_slots = 0;
  ci->size_bytes = 0;

  ClassBuild *cb = (ClassBuild *)calloc(1, sizeof(ClassBuild));
  if (!cb) {
    /* free ci */
    free(ci->name);
    free(ci->base_name);
    free(ci);
    return;
  }
  cb->ci = ci;

  /* Собираем declared members */
  const ASTNode *members = pick_members_container(class_node);
  if (members) {
    collect_members_from_node(cb, members);
  } else {
    /* fallback: по всем детям class_node (но это может захватить base/id тоже — они не vardecl/func, ок) */
    for (int i = 0; i < class_node->numChildren; i++) {
      collect_members_from_node(cb, class_node->children[i]);
    }
  }

  env_add_class(ctx->env, ci);
  builds_add(ctx, cb);
}

/* рекурсивно ищем class-nodes во всём AST */
static void walk_find_classes(BuildCtx *ctx, const ASTNode *n) {
  if (!ctx || !n) return;

  if (is_class_node(n)) {
    collect_one_class(ctx, n);
    return; /* не ищем вложенные классы внутри этого узла (можно убрать, если нужно) */
  }

  for (int i = 0; i < n->numChildren; i++) {
    walk_find_classes(ctx, n->children[i]);
  }
}

/* ========================= public API ========================= */

TypeEnv *types_build_from_ast(ASTNode *root) {
  TypeEnv *env = (TypeEnv *)calloc(1, sizeof(TypeEnv));
  if (!env) return NULL;

  BuildCtx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.env = env;

  walk_find_classes(&ctx, root);

  /* связать base pointers пока не обязательно, compute_layout сам найдёт base build по имени */

  /* compute layout for all */
  for (int i = 0; i < ctx.n_builds; i++) {
    compute_layout(&ctx, ctx.builds[i]);
  }

  /* cleanup declared buffers */
  for (int i = 0; i < ctx.n_builds; i++) {
    ClassBuild *cb = ctx.builds[i];
    if (!cb) continue;

    free_fieldinfo_array(cb->decl_fields, cb->n_decl_fields);
    free_methodinfo_array(cb->decl_methods, cb->n_decl_methods);
    free(cb);
  }
  free(ctx.builds);

  return env;
}

void types_free(TypeEnv *env) {
  if (!env) return;

  if (env->classes) {
    for (int i = 0; i < env->n; i++) {
      ClassInfo *c = env->classes[i];
      if (!c) continue;

      free(c->name);
      free(c->base_name);

      if (c->fields) free_fieldinfo_array(c->fields, c->n_fields);
      if (c->vtable) free_methodinfo_array(c->vtable, c->n_slots);

      free(c);
    }
    free(env->classes);
  }

  free(env);
}

int types_field_offset(const TypeEnv *env,
                       const char *class_name,
                       const char *field_name,
                       int *out_off) {
  if (out_off) *out_off = 0;
  if (!env || !class_name || !field_name) return 0;

  const ClassInfo *ci = types_find_class(env, class_name);
  if (!ci) return 0;

  for (int i = 0; i < ci->n_fields; i++) {
    const FieldInfo *f = &ci->fields[i];
    if (f->name && streq(f->name, field_name)) {
      if (out_off) *out_off = f->offset;
      return 1;
    }
  }
  return 0;
}

int types_method_slot_and_label(const TypeEnv *env,
                                const char *class_name,
                                const char *method_name,
                                int *out_slot,
                                const char **out_impl_label) {
  if (out_slot) *out_slot = -1;
  if (out_impl_label) *out_impl_label = NULL;
  if (!env || !class_name || !method_name) return 0;

  const ClassInfo *ci = types_find_class(env, class_name);
  if (!ci) return 0;

  for (int i = 0; i < ci->n_slots; i++) {
    const MethodInfo *m = &ci->vtable[i];
    if (m->name && streq(m->name, method_name)) {
      if (out_slot) *out_slot = m->slot;
      if (out_impl_label) *out_impl_label = m->impl_label;
      return 1;
    }
  }
  return 0;
}
