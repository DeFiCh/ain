use dioxus::prelude::*;

pub fn app(cx: Scope) -> Element {
    cx.render(rsx! {
        div {
            "dfeye: 👀"
        }
    })
}
