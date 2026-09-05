const UIAbility = globalThis.requireNapi('app.ability.UIAbility');
class EntryAbility extends UIAbility {
    onWindowStageCreate(stage) { stage.loadContent('pages/Settings'); }
}
globalThis.exports.default = EntryAbility;
