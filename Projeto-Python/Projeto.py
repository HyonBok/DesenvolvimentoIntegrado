import numpy as np

def cgnr(A, b, x0=None, tol=1e-6, max_iter=1000):
    """
    Solve Ax = b using the CGNR method.

    Parameters:
        A : ndarray (m x n)
        b : ndarray (m,)
        x0 : initial guess (n,), optional
        tol : tolerance for convergence
        max_iter : maximum number of iterations

    Returns:
        x : solution vector
        history : list of residual norms
    """

    m, n = A.shape

    if x0 is None:
        x = np.zeros(n)
    else:
        x = x0.copy()

    r = b - A @ x                # residual
    z = A.T @ r                  # transformed residual
    p = z.copy()

    history = [np.linalg.norm(r)]

    for k in range(max_iter):
        Ap = A @ p
        alpha = (z @ z) / (Ap @ Ap)

        x = x + alpha * p
        r = r - alpha * Ap
        z_new = A.T @ r

        history.append(np.linalg.norm(r))

        if np.linalg.norm(r) < tol:
            break

        beta = (z_new @ z_new) / (z @ z)
        p = z_new + beta * p
        z = z_new

    return x, history

A = np.array([[3, 2],
              [2, 6]])
b = np.array([2, -8])

x, history = cgnr(A, b)

print("Solution:", x)
print("Final residual:", history[-1])