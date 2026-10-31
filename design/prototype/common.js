// 試作の共通処理 (ネットからは何も読み込まない)

// テーマ: 「OS に合わせる → ライト → ダーク」の順に切り替える (確認用)
(function () {
  const order = ["system", "light", "dark"];
  const labels = { system: "テーマ: OS に合わせる", light: "テーマ: ライト", dark: "テーマ: ダーク" };

  function apply(mode) {
    if (mode === "system") document.documentElement.removeAttribute("data-theme");
    else document.documentElement.setAttribute("data-theme", mode);
    const button = document.querySelector(".theme-switch");
    if (button) button.textContent = labels[mode];
  }

  // ?theme=light / ?theme=dark で指定もできる (スクリーンショット用)
  let mode = new URLSearchParams(location.search).get("theme") || localStorage.getItem("prototype-theme") || "system";
  apply(mode);

  window.addEventListener("DOMContentLoaded", () => {
    const button = document.createElement("button");
    button.className = "btn theme-switch";
    button.addEventListener("click", () => {
      mode = order[(order.indexOf(mode) + 1) % order.length];
      localStorage.setItem("prototype-theme", mode);
      apply(mode);
    });
    document.body.appendChild(button);
    apply(mode);
  });
})();

// ホイールで値を増減する (設計書 4.3)。data-min / data-max / data-step / data-suffix
window.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll(".wheel[data-step]").forEach((el) => {
    const value = el.querySelector(".value");
    el.addEventListener("wheel", (e) => {
      e.preventDefault();
      const step = Number(el.dataset.step);
      const min = Number(el.dataset.min), max = Number(el.dataset.max);
      const next = Math.min(max, Math.max(min, Number(value.textContent) + (e.deltaY < 0 ? step : -step)));
      value.textContent = String(next);
    }, { passive: false });
  });

  // 押すと状態が変わるボタン (aria-pressed)
  document.querySelectorAll("[data-toggle]").forEach((el) => {
    el.addEventListener("click", () => el.setAttribute("aria-pressed", String(el.getAttribute("aria-pressed") !== "true")));
  });

  // セグメント: 1つだけ選ぶ
  document.querySelectorAll(".seg").forEach((seg) => {
    seg.querySelectorAll("button").forEach((b) => {
      b.addEventListener("click", () => {
        seg.querySelectorAll("button").forEach((o) => o.setAttribute("aria-pressed", "false"));
        b.setAttribute("aria-pressed", "true");
      });
    });
  });
});
